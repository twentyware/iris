// macOS backend. The overlay is a borderless, mouse-ignoring NSWindow at the
// shielding window level (above everything, including the menu bar) whose
// alphaValue is animated. The app is an accessory agent (no Dock icon) with an
// NSStatusBar item. A manual Cocoa event pump on the main thread drives ticks,
// mirroring the Windows loop, so events are always processed.

#import <Cocoa/Cocoa.h>

#include <chrono>
#include <memory>
#include <stdexcept>

#include "app.h"
#include "overlay.h"
#include "tray.h"

namespace iris {

/// Screen-covering overlay window with animated opacity.
class MacOverlay : public Overlay {
 public:
  MacOverlay() {
    // Union of all screen frames so the overlay spans every display.
    NSRect frame = NSZeroRect;
    for (NSScreen* screen in [NSScreen screens]) {
      frame = NSUnionRect(frame, [screen frame]);
    }
    if (NSIsEmptyRect(frame)) {
      frame = NSMakeRect(0, 0, 100, 100);
    }

    window_ = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskBorderless
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [window_ setLevel:CGShieldingWindowLevel()];
    [window_ setOpaque:NO];
    [window_ setHasShadow:NO];
    [window_ setBackgroundColor:[NSColor blackColor]];
    [window_ setAlphaValue:0.0];
    [window_ setIgnoresMouseEvents:YES];
    [window_ setCollectionBehavior:
                  NSWindowCollectionBehaviorCanJoinAllSpaces |
                  NSWindowCollectionBehaviorFullScreenAuxiliary |
                  NSWindowCollectionBehaviorStationary |
                  NSWindowCollectionBehaviorIgnoresCycle];
  }

  void show() override { [window_ orderFrontRegardless]; }

  void setAlpha(float alpha) override {
    [window_ setAlphaValue:static_cast<CGFloat>(alpha)];
  }

  void hide() override { [window_ orderOut:nil]; }

 private:
  NSWindow* window_{nil};  // retained for the process lifetime
};

}  // namespace iris

/// Objective-C target for status-bar menu actions, bridging to TrayCallbacks.
@interface IrisMenuTarget : NSObject {
 @public
  iris::TrayCallbacks callbacks;
  NSMenuItem* enabledItem;
}
- (void)toggleEnabled:(id)sender;
- (void)selectInterval:(id)sender;
- (void)quit:(id)sender;
@end

@implementation IrisMenuTarget
- (void)toggleEnabled:(id)sender {
  NSMenuItem* item = (NSMenuItem*)sender;
  BOOL enabled = ([item state] != NSControlStateValueOn);
  [item setState:enabled ? NSControlStateValueOn : NSControlStateValueOff];
  if (callbacks.onEnabledChanged) {
    callbacks.onEnabledChanged(enabled);
  }
}
- (void)selectInterval:(id)sender {
  NSMenuItem* item = (NSMenuItem*)sender;
  for (NSMenuItem* sibling in [[item menu] itemArray]) {
    [sibling setState:NSControlStateValueOff];
  }
  [item setState:NSControlStateValueOn];
  if (callbacks.onIntervalChanged) {
    callbacks.onIntervalChanged(static_cast<int>([item tag]));
  }
}
- (void)quit:(id)sender {
  if (callbacks.onQuit) {
    callbacks.onQuit();
  }
}
@end

namespace iris {

/// NSStatusBar tray icon and menu.
class MacTray : public Tray {
 public:
  explicit MacTray(const TrayCallbacks& callbacks) {
    target_ = [[IrisMenuTarget alloc] init];
    target_->callbacks = callbacks;

    statusItem_ = [[[NSStatusBar systemStatusBar]
        statusItemWithLength:NSVariableStatusItemLength] retain];
    [[statusItem_ button] setTitle:@"◐"];  // half-filled circle glyph

    NSMenu* menu = [[NSMenu alloc] init];

    NSMenuItem* enabled = [[NSMenuItem alloc] initWithTitle:@"Enabled"
                                                     action:@selector(toggleEnabled:)
                                              keyEquivalent:@""];
    [enabled setTarget:target_];
    [enabled setState:NSControlStateValueOn];
    [menu addItem:enabled];
    target_->enabledItem = enabled;

    NSMenuItem* intervalItem = [[NSMenuItem alloc] initWithTitle:@"Interval"
                                                          action:nil
                                                   keyEquivalent:@""];
    NSMenu* intervalMenu = [[NSMenu alloc] init];
    for (int preset : kIntervalPresets) {
      NSString* label = [NSString stringWithFormat:@"%d minutes", preset];
      NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:label
                                                    action:@selector(selectInterval:)
                                             keyEquivalent:@""];
      [item setTarget:target_];
      [item setTag:preset];
      [intervalMenu addItem:item];
      [intervalItems_ addObject:item];
    }
    [intervalItem setSubmenu:intervalMenu];
    [menu addItem:intervalItem];

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                  action:@selector(quit:)
                                           keyEquivalent:@""];
    [quit setTarget:target_];
    [menu addItem:quit];

    [statusItem_ setMenu:menu];
  }

  void setEnabled(bool enabled) override {
    [target_->enabledItem
        setState:enabled ? NSControlStateValueOn : NSControlStateValueOff];
  }

  void setIntervalMinutes(int minutes) override {
    for (NSMenuItem* item in intervalItems_) {
      [item setState:([item tag] == minutes) ? NSControlStateValueOn
                                             : NSControlStateValueOff];
    }
  }

 private:
  IrisMenuTarget* target_{nil};
  NSStatusItem* statusItem_{nil};
  NSMutableArray<NSMenuItem*>* intervalItems_{[[NSMutableArray alloc] init]};
};

std::unique_ptr<Overlay> createOverlay() {
  return std::make_unique<MacOverlay>();
}

std::unique_ptr<Tray> createTray(const TrayCallbacks& callbacks) {
  return std::make_unique<MacTray>(callbacks);
}

int runEventLoop(App& app, const Config& config) {
  @autoreleasepool {
    NSApplication* nsApp = [NSApplication sharedApplication];
    // Accessory agent: no Dock icon, status-bar item only.
    [nsApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [nsApp finishLaunching];

    bool running = true;
    std::unique_ptr<Tray> tray;
    if (!config.selftest) {
      TrayCallbacks callbacks;
      callbacks.onEnabledChanged = [&app](bool enabled) {
        app.setEnabled(enabled);
      };
      callbacks.onIntervalChanged = [&app](int minutes) {
        app.setIntervalMinutes(minutes);
      };
      callbacks.onQuit = [&running]() { running = false; };
      tray = createTray(callbacks);
      tray->setEnabled(app.enabled());
      tray->setIntervalMinutes(app.intervalMinutes());
    }

    const auto deadline = Clock::now() + std::chrono::seconds(30);

    while (running) {
      @autoreleasepool {
        // Drain all pending events without blocking.
        while (true) {
          NSEvent* event =
              [nsApp nextEventMatchingMask:NSEventMaskAny
                                 untilDate:[NSDate distantPast]
                                    inMode:NSDefaultRunLoopMode
                                   dequeue:YES];
          if (event == nil) {
            break;
          }
          [nsApp sendEvent:event];
        }

        app.tick(Clock::now());

        if (config.selftest) {
          if (app.completedFades() >= 1) {
            return 0;
          }
          if (Clock::now() > deadline) {
            return 1;
          }
        }

        [NSThread sleepForTimeInterval:0.015];
      }
    }
  }
  return 0;
}

}  // namespace iris
