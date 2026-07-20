// Linux backend with two overlay implementations, selected at runtime:
//
//  - WaylandOverlay: native Wayland path using the wlr-layer-shell protocol
//    through libgtk-layer-shell, which is loaded with dlopen so it stays an
//    optional runtime dependency. Chosen on Wayland sessions whose compositor
//    supports layer-shell (KDE Plasma, Sway, Hyprland, and other wlroots
//    compositors).
//  - X11Overlay: an override-redirect, input-passthrough X11 window whose
//    opacity is animated via the compositor's _NET_WM_WINDOW_OPACITY property.
//    Chosen on Xorg sessions, and on Wayland via XWayland when layer-shell is
//    unavailable — notably GNOME, whose compositor renders override-redirect
//    X windows above regular windows and honors the opacity hint, so stock
//    Ubuntu (GNOME on Wayland) works without any native Wayland path.
//
// `IRIS_BACKEND=x11|wayland` forces a specific overlay backend (used by CI and
// for debugging). The overlay is hidden between fades so nothing is visible
// when idle. The tray is an Ayatana AppIndicator driven by the GLib main loop,
// on which a timer advances the fade. All work happens on the main thread, so
// the loop is never blocked.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app.h"
#include "overlay.h"
#include "tray.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <dlfcn.h>
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>

namespace Iris {

namespace {

/// Logs an X protocol error and lets the process continue.
///
/// Xlib's default error handler calls `exit`, which would kill this long-lived
/// tray application on a transient error (e.g. operating on a window whose
/// screen was just unplugged). Reminders are best-effort, so we log and carry
/// on instead.
int ignore_x_error(Display *display, XErrorEvent *error) {
  char message[128];
  XGetErrorText(display, error->error_code, message, sizeof(message));
  std::fprintf(stderr, "iris: non-fatal X error: %s\n", message);
  return 0;
}

} // namespace

/// X11 override-redirect overlay with compositor-based opacity fading.
///
/// One window is created on every X screen so the fade appears on all displays
/// at once; within a single X screen the window spans the whole root, so it
/// also covers every RandR/Xinerama monitor. Geometry is re-queried on each
/// `show()` so a resolution or monitor change between reminders is picked up.
class X11Overlay : public Overlay {
public:
  X11Overlay() {
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) {
      throw std::runtime_error(
        "Cannot open an X11 display. Iris needs either an X11 connection "
        "(an Xorg session, or XWayland within a Wayland session) or a Wayland "
        "compositor that supports layer-shell plus the gtk-layer-shell library."
      );
    }
    // Keep transient protocol errors from aborting this long-lived process.
    XSetErrorHandler(&ignore_x_error);
    opacity_atom_ = XInternAtom(display_, "_NET_WM_WINDOW_OPACITY", False);

    const int screen_count = ScreenCount(display_);
    for (int screen = 0; screen < screen_count; ++screen) {
      Window root = RootWindow(display_, screen);

      XSetWindowAttributes window_attributes = {};
      window_attributes.override_redirect = True; // bypass the window manager
      window_attributes.background_pixel = BlackPixel(display_, screen);
      window_attributes.event_mask = 0; // we never read input events

      Window window = XCreateWindow(
        display_, root, 0, 0, static_cast<unsigned>(DisplayWidth(display_, screen)),
        static_cast<unsigned>(DisplayHeight(display_, screen)), 0, CopyFromParent, InputOutput,
        CopyFromParent, CWOverrideRedirect | CWBackPixel, &window_attributes
      );

      // Make the window click-through: give it an empty input shape region.
      XserverRegion region = XFixesCreateRegion(display_, nullptr, 0);
      XFixesSetWindowShapeRegion(display_, window, ShapeInput, 0, 0, region);
      XFixesDestroyRegion(display_, region);

      screens_.push_back({window, root});
    }

    set_alpha(0.0F);
    XFlush(display_);
  }

  ~X11Overlay() override {
    if (display_ != nullptr) {
      for (const ScreenWindow &screen : screens_) {
        XDestroyWindow(display_, screen.window);
      }
      XCloseDisplay(display_);
    }
  }

  X11Overlay(const X11Overlay &) = delete;
  X11Overlay &operator=(const X11Overlay &) = delete;

  void show() override {
    for (const ScreenWindow &screen : screens_) {
      // Re-fit to the current root size in case the resolution or monitor
      // layout changed since the last reminder.
      Window root_return = 0;
      int x = 0;
      int y = 0;
      unsigned width = 0;
      unsigned height = 0;
      unsigned border = 0;
      unsigned depth = 0;
      if (
        XGetGeometry(
          display_, screen.root, &root_return, &x, &y, &width, &height, &border, &depth
        ) != 0 &&
        width > 0 && height > 0
      ) {
        XMoveResizeWindow(display_, screen.window, 0, 0, width, height);
      }
      XMapRaised(display_, screen.window);
      XRaiseWindow(display_, screen.window);
    }
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
    for (const ScreenWindow &screen : screens_) {
      XChangeProperty(
        display_, screen.window, opacity_atom_, XA_CARDINAL, 32, PropModeReplace,
        reinterpret_cast<const unsigned char *>(&opacity), 1
      );
    }
    XFlush(display_);
  }

  void hide() override {
    for (const ScreenWindow &screen : screens_) {
      XUnmapWindow(display_, screen.window);
    }
    XFlush(display_);
  }

private:
  /// An overlay window paired with the root it covers.
  struct ScreenWindow {
    Window window;
    Window root;
  };

  Display *display_{nullptr};
  Atom opacity_atom_{0};
  std::vector<ScreenWindow> screens_;
};

/// Native Wayland overlay: one layer-shell surface per monitor, painted
/// translucent black at the current alpha.
///
/// libgtk-layer-shell is loaded with dlopen so the same binary runs on systems
/// without it (falling back to X11/XWayland); the handful of functions used
/// below are a stable part of its ABI since 0.5. Layer-shell surfaces on the
/// overlay layer sit above regular windows, take no keyboard focus, and get an
/// empty input region, so like the X11 overlay they are click-through and
/// never steal focus.
///
/// - Invariant: `library_ != nullptr` and every resolved function pointer is
///   non-null after construction (the constructor throws otherwise).
class WaylandOverlay : public Overlay {
public:
  WaylandOverlay() {
    if (std::getenv("WAYLAND_DISPLAY") == nullptr) {
      throw std::runtime_error("not a Wayland session");
    }
    library_ = dlopen("libgtk-layer-shell.so.0", RTLD_NOW | RTLD_LOCAL);
    if (library_ == nullptr) {
      library_ = dlopen("libgtk-layer-shell.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (library_ == nullptr) {
      throw std::runtime_error("gtk-layer-shell is not installed");
    }
    layer_is_supported_ = resolve<IsSupportedFn>("gtk_layer_is_supported");
    layer_init_for_window_ = resolve<InitForWindowFn>("gtk_layer_init_for_window");
    layer_set_layer_ = resolve<SetIntFn>("gtk_layer_set_layer");
    layer_set_anchor_ = resolve<SetAnchorFn>("gtk_layer_set_anchor");
    layer_set_exclusive_zone_ = resolve<SetIntFn>("gtk_layer_set_exclusive_zone");
    layer_set_monitor_ = resolve<SetMonitorFn>("gtk_layer_set_monitor");
    layer_set_namespace_ = resolve<SetNamespaceFn>("gtk_layer_set_namespace");

    // The overlay is created before the platform event loop starts, so make
    // sure GTK is up; extra calls after a successful init are no-ops.
    if (gtk_init_check(nullptr, nullptr) == FALSE) {
      throw std::runtime_error("cannot initialize GTK");
    }
    if (layer_is_supported_() == FALSE) {
      // The library is present but the compositor lacks zwlr_layer_shell_v1
      // (e.g. GNOME), or GDK connected through its X11 backend.
      throw std::runtime_error("the compositor does not support layer-shell");
    }

    create_windows();
  }

  ~WaylandOverlay() override {
    destroy_windows();
    // library_ is intentionally never dlclosed: gtk-layer-shell hooks into GTK
    // internals, and unloading it while GTK is live would leave dangling
    // callbacks behind.
  }

  WaylandOverlay(const WaylandOverlay &) = delete;
  WaylandOverlay &operator=(const WaylandOverlay &) = delete;

  void show() override {
    // Re-enumerate monitors in case one was added or removed since the last
    // reminder; anchoring handles resolution changes on its own.
    GdkDisplay *display = gdk_display_get_default();
    if (gdk_display_get_n_monitors(display) != static_cast<int>(windows_.size())) {
      destroy_windows();
      create_windows();
    }
    for (GtkWidget *window : windows_) {
      gtk_widget_show_all(window);
    }
  }

  void set_alpha(float alpha) override {
    if (alpha < 0.0F)
      alpha = 0.0F;
    if (alpha > 1.0F)
      alpha = 1.0F;
    alpha_ = alpha;
    for (GtkWidget *window : windows_) {
      gtk_widget_queue_draw(window);
    }
  }

  void hide() override {
    for (GtkWidget *window : windows_) {
      gtk_widget_hide(window);
    }
  }

private:
  // ABI mirror of the gtk-layer-shell enums used here (gtk-layer-shell.h):
  // GtkLayerShellLayer { BACKGROUND = 0, BOTTOM = 1, TOP = 2, OVERLAY = 3 }
  // GtkLayerShellEdge { LEFT = 0, RIGHT = 1, TOP = 2, BOTTOM = 3 }
  static constexpr int layer_overlay = 3;
  static constexpr int edge_count = 4;

  using IsSupportedFn = gboolean (*)();
  using InitForWindowFn = void (*)(GtkWindow *);
  using SetIntFn = void (*)(GtkWindow *, int);
  using SetAnchorFn = void (*)(GtkWindow *, int, gboolean);
  using SetMonitorFn = void (*)(GtkWindow *, GdkMonitor *);
  using SetNamespaceFn = void (*)(GtkWindow *, const char *);

  template <typename Fn>
  Fn resolve(const char *name) {
    void *symbol = dlsym(library_, name);
    if (symbol == nullptr) {
      throw std::runtime_error(std::string("gtk-layer-shell is too old: missing ") + name);
    }
    return reinterpret_cast<Fn>(symbol);
  }

  /// Creates one full-screen, click-through overlay-layer surface per monitor.
  void create_windows() {
    GdkDisplay *display = gdk_display_get_default();
    const int monitor_count = gdk_display_get_n_monitors(display);
    for (int i = 0; i < monitor_count; ++i) {
      GdkMonitor *monitor = gdk_display_get_monitor(display, i);
      GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

      // Layer-shell setup must happen before the window is realized.
      layer_init_for_window_(GTK_WINDOW(window));
      layer_set_namespace_(GTK_WINDOW(window), "iris-dim");
      layer_set_layer_(GTK_WINDOW(window), layer_overlay);
      for (int edge = 0; edge < edge_count; ++edge) {
        // Anchoring to all four edges stretches the surface over the monitor.
        layer_set_anchor_(GTK_WINDOW(window), edge, TRUE);
      }
      // Ignore other surfaces' exclusive zones (panels, docks): cover them too.
      layer_set_exclusive_zone_(GTK_WINDOW(window), -1);
      layer_set_monitor_(GTK_WINDOW(window), monitor);

      // Translucent rendering: paint the window ourselves with an RGBA visual.
      gtk_widget_set_app_paintable(window, TRUE);
      GdkVisual *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(window));
      if (visual != nullptr) {
        gtk_widget_set_visual(window, visual);
      }
      g_signal_connect(window, "draw", G_CALLBACK(&WaylandOverlay::on_draw), this);

      // Click-through: an empty input region, like the X11 overlay's XFixes
      // shape. Requires the window to be realized.
      gtk_widget_realize(window);
      cairo_region_t *empty = cairo_region_create();
      gtk_widget_input_shape_combine_region(window, empty);
      cairo_region_destroy(empty);

      windows_.push_back(window);
    }
  }

  void destroy_windows() {
    for (GtkWidget *window : windows_) {
      gtk_widget_destroy(window);
    }
    windows_.clear();
  }

  static gboolean on_draw(GtkWidget * /*widget*/, cairo_t *context, gpointer data) {
    const auto *self = static_cast<WaylandOverlay *>(data);
    // SOURCE replaces the buffer instead of blending, so a lower alpha on the
    // next frame does not accumulate on top of the previous one.
    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(context, 0.0, 0.0, 0.0, static_cast<double>(self->alpha_));
    cairo_paint(context);
    return TRUE;
  }

  void *library_{nullptr};
  IsSupportedFn layer_is_supported_{nullptr};
  InitForWindowFn layer_init_for_window_{nullptr};
  SetIntFn layer_set_layer_{nullptr};
  SetAnchorFn layer_set_anchor_{nullptr};
  SetIntFn layer_set_exclusive_zone_{nullptr};
  SetMonitorFn layer_set_monitor_{nullptr};
  SetNamespaceFn layer_set_namespace_{nullptr};
  std::vector<GtkWidget *> windows_;
  float alpha_{0.0F};
};

std::unique_ptr<Overlay> create_overlay() {
  const char *forced = std::getenv("IRIS_BACKEND");
  const std::string requested = forced != nullptr ? forced : "";
  if (requested == "x11") {
    return std::make_unique<X11Overlay>();
  }
  if (requested == "wayland") {
    return std::make_unique<WaylandOverlay>();
  }
  if (!requested.empty()) {
    throw std::runtime_error("unknown IRIS_BACKEND (expected \"x11\" or \"wayland\")");
  }

  if (std::getenv("WAYLAND_DISPLAY") != nullptr) {
    try {
      auto overlay = std::make_unique<WaylandOverlay>();
      std::fprintf(stderr, "iris: overlay backend: wayland (layer-shell)\n");
      return overlay;
    } catch (const std::exception &error) {
      // Expected on GNOME (no layer-shell): XWayland handles it instead.
      std::fprintf(stderr, "iris: overlay backend: x11 (%s)\n", error.what());
    }
  }
  return std::make_unique<X11Overlay>();
}

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
    // one fade. CI runs this under xvfb (X11) and a headless sway (Wayland).
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (application.completed_fades() < 1) {
      application.tick(Clock::now());
      // The Wayland overlay renders through GTK, which only commits frames
      // when the GLib main context runs; a no-op for the X11 backend.
      while (g_main_context_iteration(nullptr, FALSE) != FALSE) {
      }
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
