// Linux (X11) backend. The overlay is an override-redirect, input-passthrough
// window whose opacity is animated via the compositor's
// _NET_WM_WINDOW_OPACITY property; it is unmapped between fades so nothing is
// visible when idle. The tray is an Ayatana AppIndicator driven by the GLib
// main loop, on which a timer advances the fade. All work happens on the main
// thread, so the loop is never blocked.
//
// This targets X11 sessions. Under Wayland there is no client-side always-on-
// top overlay protocol; log in to an Xorg session for Iris to work.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>

#include "app.h"
#include "overlay.h"
#include "tray.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>

namespace iris {

/// X11 override-redirect overlay with compositor-based opacity fading.
class LinuxOverlay : public Overlay {
 public:
  LinuxOverlay() {
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) {
      throw std::runtime_error(
          "Cannot open X11 display. Iris requires an Xorg session "
          "(Wayland is not supported).");
    }
    const int screen = DefaultScreen(display_);
    Window root = RootWindow(display_, screen);
    const int width = DisplayWidth(display_, screen);
    const int height = DisplayHeight(display_, screen);

    XSetWindowAttributes attrs = {};
    attrs.override_redirect = True;  // bypass the window manager
    attrs.background_pixel = BlackPixel(display_, screen);
    attrs.event_mask = 0;  // we never read input events

    window_ = XCreateWindow(
        display_, root, 0, 0, static_cast<unsigned>(width),
        static_cast<unsigned>(height), 0, CopyFromParent, InputOutput,
        CopyFromParent, CWOverrideRedirect | CWBackPixel, &attrs);

    opacityAtom_ = XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False);

    // Make the window click-through: give it an empty input shape region.
    XserverRegion region = XFixesCreateRegion(display_, nullptr, 0);
    XFixesSetWindowShapeRegion(display_, window_, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(display_, region);

    setAlpha(0.0F);
    XFlush(display_);
  }

  ~LinuxOverlay() override {
    if (display_ != nullptr) {
      if (window_ != 0) {
        XDestroyWindow(display_, window_);
      }
      XCloseDisplay(display_);
    }
  }

  LinuxOverlay(const LinuxOverlay&) = delete;
  LinuxOverlay& operator=(const LinuxOverlay&) = delete;

  void show() override {
    XMapRaised(display_, window_);
    XRaiseWindow(display_, window_);
    XFlush(display_);
  }

  void setAlpha(float alpha) override {
    if (alpha < 0.0F) alpha = 0.0F;
    if (alpha > 1.0F) alpha = 1.0F;
    // _NET_WM_WINDOW_OPACITY is a CARD32; Xlib format-32 data is passed as an
    // array of `long` (truncated to 32 bits by the server).
    const unsigned long opacity =
        static_cast<unsigned long>(static_cast<double>(alpha) * 0xFFFFFFFFu);
    XChangeProperty(display_, window_, opacityAtom_, XA_CARDINAL, 32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&opacity), 1);
    XFlush(display_);
  }

  void hide() override {
    XUnmapWindow(display_, window_);
    XFlush(display_);
  }

 private:
  Display* display_{nullptr};
  Window window_{0};
  Atom opacityAtom_{0};
};

/// Ayatana AppIndicator tray with an Enabled toggle, interval presets, and Quit.
class LinuxTray : public Tray {
 public:
  explicit LinuxTray(TrayCallbacks callbacks)
      : callbacks_(std::move(callbacks)) {
    indicator_ = app_indicator_new("iris", "display-brightness-symbolic",
                                   APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(indicator_, APP_INDICATOR_STATUS_ACTIVE);

    GtkWidget* menu = gtk_menu_new();

    enabledItem_ = gtk_check_menu_item_new_with_label("Enabled");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(enabledItem_), TRUE);
    g_signal_connect(enabledItem_, "toggled",
                     G_CALLBACK(&LinuxTray::onEnabledToggled), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), enabledItem_);

    GtkWidget* intervalItem = gtk_menu_item_new_with_label("Interval");
    GtkWidget* intervalMenu = gtk_menu_new();
    GSList* group = nullptr;
    for (int preset : kIntervalPresets) {
      char label[32];
      std::snprintf(label, sizeof(label), "%d minutes", preset);
      GtkWidget* item = gtk_radio_menu_item_new_with_label(group, label);
      group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
      g_object_set_data(G_OBJECT(item), "minutes",
                        GINT_TO_POINTER(preset));
      g_signal_connect(item, "toggled",
                       G_CALLBACK(&LinuxTray::onIntervalToggled), this);
      gtk_menu_shell_append(GTK_MENU_SHELL(intervalMenu), item);
      intervalItems_ = g_slist_append(intervalItems_, item);
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(intervalItem), intervalMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), intervalItem);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget* quitItem = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quitItem, "activate", G_CALLBACK(&LinuxTray::onQuit),
                     this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quitItem);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator_, GTK_MENU(menu));
  }

  ~LinuxTray() override {
    if (intervalItems_ != nullptr) {
      g_slist_free(intervalItems_);
    }
  }

  LinuxTray(const LinuxTray&) = delete;
  LinuxTray& operator=(const LinuxTray&) = delete;

  void setEnabled(bool enabled) override {
    suppress_ = true;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(enabledItem_),
                                   enabled ? TRUE : FALSE);
    suppress_ = false;
  }

  void setIntervalMinutes(int minutes) override {
    suppress_ = true;
    for (GSList* node = intervalItems_; node != nullptr; node = node->next) {
      auto* item = static_cast<GtkWidget*>(node->data);
      const int value = GPOINTER_TO_INT(
          g_object_get_data(G_OBJECT(item), "minutes"));
      if (value == minutes) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
      }
    }
    suppress_ = false;
  }

 private:
  static void onEnabledToggled(GtkCheckMenuItem* item, gpointer data) {
    auto* self = static_cast<LinuxTray*>(data);
    if (self->suppress_) {
      return;
    }
    const bool enabled = gtk_check_menu_item_get_active(item) != FALSE;
    if (self->callbacks_.onEnabledChanged) {
      self->callbacks_.onEnabledChanged(enabled);
    }
  }

  static void onIntervalToggled(GtkCheckMenuItem* item, gpointer data) {
    auto* self = static_cast<LinuxTray*>(data);
    if (self->suppress_ || gtk_check_menu_item_get_active(item) == FALSE) {
      return;
    }
    const int minutes =
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "minutes"));
    if (self->callbacks_.onIntervalChanged) {
      self->callbacks_.onIntervalChanged(minutes);
    }
  }

  static void onQuit(GtkMenuItem* /*item*/, gpointer data) {
    auto* self = static_cast<LinuxTray*>(data);
    if (self->callbacks_.onQuit) {
      self->callbacks_.onQuit();
    }
  }

  TrayCallbacks callbacks_;
  AppIndicator* indicator_{nullptr};
  GtkWidget* enabledItem_{nullptr};
  GSList* intervalItems_{nullptr};
  bool suppress_{false};
};

std::unique_ptr<Overlay> createOverlay() {
  return std::make_unique<LinuxOverlay>();
}

std::unique_ptr<Tray> createTray(const TrayCallbacks& callbacks) {
  return std::make_unique<LinuxTray>(callbacks);
}

namespace {

gboolean tickThunk(gpointer data) {
  auto* app = static_cast<App*>(data);
  app->tick(Clock::now());
  return G_SOURCE_CONTINUE;
}

}  // namespace

int runEventLoop(App& app, const Config& config) {
  if (config.selftest) {
    // Headless-friendly loop: no tray, just drive the overlay and exit after
    // one fade. CI runs this under xvfb.
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (app.completedFades() < 1) {
      app.tick(Clock::now());
      if (Clock::now() > deadline) {
        return 1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return 0;
  }

  gtk_init(nullptr, nullptr);

  TrayCallbacks callbacks;
  callbacks.onEnabledChanged = [&app](bool enabled) {
    app.setEnabled(enabled);
  };
  callbacks.onIntervalChanged = [&app](int minutes) {
    app.setIntervalMinutes(minutes);
  };
  callbacks.onQuit = []() { gtk_main_quit(); };

  auto tray = createTray(callbacks);
  tray->setEnabled(app.enabled());
  tray->setIntervalMinutes(app.intervalMinutes());

  g_timeout_add(15, &tickThunk, &app);
  gtk_main();
  return 0;
}

}  // namespace iris
