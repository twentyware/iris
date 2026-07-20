// macOS backend. The overlay is a borderless, mouse-ignoring NSWindow at the
// shielding window level (above everything, including the menu bar) whose
// alphaValue is animated. The application is an accessory agent (no Dock icon) with an
// NSStatusBar item. A manual Cocoa event pump on the main thread drives ticks,
// mirroring the Windows loop, so events are always processed.

#import <Cocoa/Cocoa.h>

#include <chrono>
#include <memory>
#include <stdexcept>

#include "app.h"
#include "overlay.h"
#include "tray.h"

namespace Iris {

/// Screen-covering overlay window with animated opacity.
class MacOverlay : public Overlay {
public:
  MacOverlay() {
    window_ = [[NSWindow alloc] initWithContentRect:all_screens_frame()
                                          styleMask:NSWindowStyleMaskBorderless
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [window_ setLevel:CGShieldingWindowLevel()];
    [window_ setOpaque:NO];
    [window_ setHasShadow:NO];
    [window_ setBackgroundColor:[NSColor blackColor]];
    [window_ setAlphaValue:0.0];
    [window_ setIgnoresMouseEvents:YES];
    [window_ setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
                                   NSWindowCollectionBehaviorFullScreenAuxiliary |
                                   NSWindowCollectionBehaviorStationary |
                                   NSWindowCollectionBehaviorIgnoresCycle];
  }

  void show() override {
    // Re-fit to the union of the current screens so every display is covered
    // even if one was added, removed, or resized since the last reminder.
    [window_ setFrame:all_screens_frame() display:NO];
    [window_ orderFrontRegardless];
  }

  void set_alpha(float alpha) override { [window_ setAlphaValue:static_cast<CGFloat>(alpha)]; }

  void hide() override { [window_ orderOut:nil]; }

private:
  /// The rectangle spanning every attached display (a small fallback if none).
  static NSRect all_screens_frame() {
    NSRect frame = NSZeroRect;
    for (NSScreen *screen in [NSScreen screens]) {
      frame = NSUnionRect(frame, [screen frame]);
    }
    if (NSIsEmptyRect(frame)) {
      frame = NSMakeRect(0, 0, 100, 100);
    }
    return frame;
  }

  NSWindow *window_{nil}; // retained for the process lifetime
};

} // namespace Iris

/// Objective-C target for status-bar menu actions, bridging to TrayCallbacks.
@interface IrisMenuTarget : NSObject {
@public
  Iris::TrayCallbacks callbacks;
  NSMenuItem *enabled_item;
}
- (void)toggle_enabled:(id)sender;
- (void)select_interval:(id)sender;
- (void)quit:(id)sender;
@end

@implementation IrisMenuTarget
- (void)toggle_enabled:(id)sender {
  NSMenuItem *item = (NSMenuItem *)sender;
  BOOL enabled = ([item state] != NSControlStateValueOn);
  [item setState:enabled ? NSControlStateValueOn : NSControlStateValueOff];
  if (callbacks.on_enabled_changed) {
    callbacks.on_enabled_changed(enabled);
  }
}
- (void)select_interval:(id)sender {
  NSMenuItem *item = (NSMenuItem *)sender;
  for (NSMenuItem *sibling in [[item menu] itemArray]) {
    [sibling setState:NSControlStateValueOff];
  }
  [item setState:NSControlStateValueOn];
  if (callbacks.on_interval_changed) {
    callbacks.on_interval_changed(static_cast<int>([item tag]));
  }
}
- (void)quit:(id)sender {
  if (callbacks.on_quit) {
    callbacks.on_quit();
  }
}
@end

namespace Iris {

/// NSStatusBar tray icon and menu.
class MacTray : public Tray {
public:
  explicit MacTray(const TrayCallbacks &callbacks) {
    menu_target_ = [[IrisMenuTarget alloc] init];
    menu_target_->callbacks = callbacks;

    status_item_ =
      [[[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength] retain];
    [[status_item_ button] setTitle:@"◐"]; // half-filled circle glyph

    NSMenu *menu = [[NSMenu alloc] init];

    NSMenuItem *enabled = [[NSMenuItem alloc] initWithTitle:@"Enabled"
                                                     action:@selector(toggle_enabled:)
                                              keyEquivalent:@""];
    [enabled setTarget:menu_target_];
    [enabled setState:NSControlStateValueOn];
    [menu addItem:enabled];
    menu_target_->enabled_item = enabled;

    NSMenuItem *interval_item = [[NSMenuItem alloc] initWithTitle:@"Interval"
                                                           action:nil
                                                    keyEquivalent:@""];
    NSMenu *interval_menu = [[NSMenu alloc] init];
    for (int preset : interval_presets) {
      NSString *label = [NSString stringWithFormat:@"%d minutes", preset];
      NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:label
                                                    action:@selector(select_interval:)
                                             keyEquivalent:@""];
      [item setTarget:menu_target_];
      [item setTag:preset];
      [interval_menu addItem:item];
      [interval_items_ addObject:item];
    }
    [interval_item setSubmenu:interval_menu];
    [menu addItem:interval_item];

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                  action:@selector(quit:)
                                           keyEquivalent:@""];
    [quit setTarget:menu_target_];
    [menu addItem:quit];

    [status_item_ setMenu:menu];
  }

  void set_enabled(bool enabled) override {
    [menu_target_->enabled_item setState:enabled ? NSControlStateValueOn : NSControlStateValueOff];
  }

  void set_interval_minutes(int minutes) override {
    for (NSMenuItem *item in interval_items_) {
      [item setState:([item tag] == minutes) ? NSControlStateValueOn : NSControlStateValueOff];
    }
  }

private:
  IrisMenuTarget *menu_target_{nil};
  NSStatusItem *status_item_{nil};
  NSMutableArray<NSMenuItem *> *interval_items_{[[NSMutableArray alloc] init]};
};

std::unique_ptr<Overlay> create_overlay() { return std::make_unique<MacOverlay>(); }

std::unique_ptr<Tray> create_tray(const TrayCallbacks &callbacks) {
  return std::make_unique<MacTray>(callbacks);
}

int run_event_loop(App &application, const Config &config) {
  @autoreleasepool {
    NSApplication *ns_application = [NSApplication sharedApplication];
    // Accessory agent: no Dock icon, status-bar item only.
    [ns_application setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [ns_application finishLaunching];

    bool running = true;
    std::unique_ptr<Tray> tray_icon;
    if (!config.selftest) {
      TrayCallbacks callbacks;
      callbacks.on_enabled_changed = [&application](bool enabled) {
        application.set_enabled(enabled);
      };
      callbacks.on_interval_changed = [&application](int minutes) {
        application.set_interval_minutes(minutes);
      };
      callbacks.on_quit = [&running]() { running = false; };
      tray_icon = create_tray(callbacks);
      tray_icon->set_enabled(application.enabled());
      tray_icon->set_interval_minutes(application.interval_minutes());
    }

    const auto deadline = Clock::now() + std::chrono::seconds(30);

    while (running) {
      @autoreleasepool {
        // Drain all pending events without blocking.
        while (true) {
          NSEvent *event = [ns_application nextEventMatchingMask:NSEventMaskAny
                                                       untilDate:[NSDate distantPast]
                                                          inMode:NSDefaultRunLoopMode
                                                         dequeue:YES];
          if (event == nil) {
            break;
          }
          [ns_application sendEvent:event];
        }

        application.tick(Clock::now());

        if (config.selftest) {
          if (application.completed_fades() >= 1) {
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

} // namespace Iris
