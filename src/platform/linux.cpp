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

namespace Iris {

/// X11 override-redirect overlay with compositor-based opacity fading.
class LinuxOverlay : public Overlay {
public:
  LinuxOverlay() {
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) {
      throw std::runtime_error(
        "Cannot open X11 display. Iris requires an Xorg session "
        "(Wayland is not supported)."
      );
    }
    const int screen = DefaultScreen(display_);
    Window root = RootWindow(display_, screen);
    const int width = DisplayWidth(display_, screen);
    const int height = DisplayHeight(display_, screen);

    XSetWindowAttributes window_attributes = {};
    window_attributes.override_redirect = True; // bypass the window manager
    window_attributes.background_pixel = BlackPixel(display_, screen);
    window_attributes.event_mask = 0; // we never read input events

    window_ = XCreateWindow(
      display_, root, 0, 0, static_cast<unsigned>(width), static_cast<unsigned>(height), 0,
      CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel,
      &window_attributes
    );

    opacity_atom_ = XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False);

    // Make the window click-through: give it an empty input shape region.
    XserverRegion region = XFixesCreateRegion(display_, nullptr, 0);
    XFixesSetWindowShapeRegion(display_, window_, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(display_, region);

    set_alpha(0.0F);
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

  LinuxOverlay(const LinuxOverlay &) = delete;
  LinuxOverlay &operator=(const LinuxOverlay &) = delete;

  void show() override {
    XMapRaised(display_, window_);
    XRaiseWindow(display_, window_);
    XFlush(display_);
  }

  void set_alpha(float alpha) override {
    if (alpha < 0.0F)
      alpha = 0.0F;
    if (alpha > 1.0F)
      alpha = 1.0F;
    // _NET_WM_WINDOW_OPACITY is a CARD32; Xlib format-32 data is passed as an
    // array of `long` (truncated to 32 bits by the server).
    const unsigned long opacity =
      static_cast<unsigned long>(static_cast<double>(alpha) * 0xFFFFFFFFu);
    XChangeProperty(
      display_, window_, opacity_atom_, XA_CARDINAL, 32, PropModeReplace,
      reinterpret_cast<const unsigned char *>(&opacity), 1
    );
    XFlush(display_);
  }

  void hide() override {
    XUnmapWindow(display_, window_);
    XFlush(display_);
  }

private:
  Display *display_{nullptr};
  Window window_{0};
  Atom opacity_atom_{0};
};

/// Ayatana AppIndicator tray with an Enabled toggle, interval presets, and Quit.
class LinuxTray : public Tray {
public:
  explicit LinuxTray(TrayCallbacks callbacks) : callbacks_(std::move(callbacks)) {
    indicator_ = app_indicator_new(
      "iris", "display-brightness-symbolic", APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );
    app_indicator_set_status(indicator_, APP_INDICATOR_STATUS_ACTIVE);

    GtkWidget *menu = gtk_menu_new();

    enabled_item_ = gtk_check_menu_item_new_with_label("Enabled");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(enabled_item_), TRUE);
    g_signal_connect(enabled_item_, "toggled", G_CALLBACK(&LinuxTray::on_enabled_toggled), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), enabled_item_);

    GtkWidget *interval_item = gtk_menu_item_new_with_label("Interval");
    GtkWidget *interval_menu = gtk_menu_new();
    GSList *group = nullptr;
    for (int preset : interval_presets) {
      char label[32];
      std::snprintf(label, sizeof(label), "%d minutes", preset);
      GtkWidget *item = gtk_radio_menu_item_new_with_label(group, label);
      group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
      g_object_set_data(G_OBJECT(item), "minutes", GINT_TO_POINTER(preset));
      g_signal_connect(item, "toggled", G_CALLBACK(&LinuxTray::on_interval_toggled), this);
      gtk_menu_shell_append(GTK_MENU_SHELL(interval_menu), item);
      interval_items_ = g_slist_append(interval_items_, item);
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(interval_item), interval_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), interval_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(&LinuxTray::on_quit), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator_, GTK_MENU(menu));
  }

  ~LinuxTray() override {
    if (interval_items_ != nullptr) {
      g_slist_free(interval_items_);
    }
  }

  LinuxTray(const LinuxTray &) = delete;
  LinuxTray &operator=(const LinuxTray &) = delete;

  void set_enabled(bool enabled) override {
    suppress_ = true;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(enabled_item_), enabled ? TRUE : FALSE);
    suppress_ = false;
  }

  void set_interval_minutes(int minutes) override {
    suppress_ = true;
    for (GSList *node = interval_items_; node != nullptr; node = node->next) {
      auto *item = static_cast<GtkWidget *>(node->data);
      const int preset_minutes = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "minutes"));
      if (preset_minutes == minutes) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
      }
    }
    suppress_ = false;
  }

private:
  static void on_enabled_toggled(GtkCheckMenuItem *item, gpointer data) {
    auto *self = static_cast<LinuxTray *>(data);
    if (self->suppress_) {
      return;
    }
    const bool enabled = gtk_check_menu_item_get_active(item) != FALSE;
    if (self->callbacks_.on_enabled_changed) {
      self->callbacks_.on_enabled_changed(enabled);
    }
  }

  static void on_interval_toggled(GtkCheckMenuItem *item, gpointer data) {
    auto *self = static_cast<LinuxTray *>(data);
    if (self->suppress_ || gtk_check_menu_item_get_active(item) == FALSE) {
      return;
    }
    const int minutes = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "minutes"));
    if (self->callbacks_.on_interval_changed) {
      self->callbacks_.on_interval_changed(minutes);
    }
  }

  static void on_quit(GtkMenuItem * /*item*/, gpointer data) {
    auto *self = static_cast<LinuxTray *>(data);
    if (self->callbacks_.on_quit) {
      self->callbacks_.on_quit();
    }
  }

  TrayCallbacks callbacks_;
  AppIndicator *indicator_{nullptr};
  GtkWidget *enabled_item_{nullptr};
  GSList *interval_items_{nullptr};
  bool suppress_{false};
};

std::unique_ptr<Overlay> create_overlay() { return std::make_unique<LinuxOverlay>(); }

std::unique_ptr<Tray> create_tray(const TrayCallbacks &callbacks) {
  return std::make_unique<LinuxTray>(callbacks);
}

namespace {

gboolean tick_thunk(gpointer data) {
  auto *application = static_cast<App *>(data);
  application->tick(Clock::now());
  return G_SOURCE_CONTINUE;
}

} // namespace

int run_event_loop(App &application, const Config &config) {
  if (config.selftest) {
    // Headless-friendly loop: no tray, just drive the overlay and exit after
    // one fade. CI runs this under xvfb.
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (application.completed_fades() < 1) {
      application.tick(Clock::now());
      if (Clock::now() > deadline) {
        return 1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return 0;
  }

  gtk_init(nullptr, nullptr);

  TrayCallbacks callbacks;
  callbacks.on_enabled_changed = [&application](bool enabled) { application.set_enabled(enabled); };
  callbacks.on_interval_changed = [&application](int minutes) {
    application.set_interval_minutes(minutes);
  };
  callbacks.on_quit = []() { gtk_main_quit(); };

  auto tray_icon = create_tray(callbacks);
  tray_icon->set_enabled(application.enabled());
  tray_icon->set_interval_minutes(application.interval_minutes());

  g_timeout_add(15, &tick_thunk, &application);
  gtk_main();
  return 0;
}

} // namespace Iris
