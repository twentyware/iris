// Windows backend: a layered, topmost, click-through, no-activate tool window
// used as the fade overlay, plus a Shell_NotifyIcon tray icon. Everything runs
// on a single thread with a proper PeekMessage pump, which is the fix for the
// original freeze (the old overlay lived on a thread with no message loop).

// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <chrono>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

#include "app.h"
#include "overlay.h"
#include "tray.h"

#undef min
#undef max

namespace Iris {

namespace {

constexpr wchar_t overlay_class[] = L"IrisOverlayClass";
constexpr wchar_t tray_class[] = L"IrisTrayClass";
constexpr UINT tray_callback_message = WM_USER + 1;
constexpr UINT tray_icon_id = 1;

// Menu command ids. Interval presets occupy a contiguous range so the handler
// can map a command back to interval_presets by offset.
constexpr UINT cmd_enable = 100;
constexpr UINT cmd_quit = 101;
constexpr UINT cmd_interval_base = 200;

} // namespace

/// Layered full-screen overlay window.
class WindowsOverlay : public Overlay {
public:
  WindowsOverlay() {
    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = overlay_class;
    window_class.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    RegisterClassW(&window_class);

    const RECT bounds = virtual_screen_bounds();
    window_handle_ = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
        WS_EX_TOOLWINDOW, // no taskbar button, click-through, no focus
      overlay_class, L"Iris Overlay", WS_POPUP, bounds.left, bounds.top, bounds.right - bounds.left,
      bounds.bottom - bounds.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    if (window_handle_ == nullptr) {
      throw std::runtime_error("Failed to create overlay window");
    }
    SetLayeredWindowAttributes(window_handle_, RGB(0, 0, 0), 0, LWA_ALPHA);
  }

  ~WindowsOverlay() override {
    if (window_handle_ != nullptr) {
      DestroyWindow(window_handle_);
    }
  }

  WindowsOverlay(const WindowsOverlay &) = delete;
  WindowsOverlay &operator=(const WindowsOverlay &) = delete;

  void show() override {
    // Re-fit to the whole virtual desktop so every monitor is covered even if
    // the display layout changed since the window was created.
    const RECT bounds = virtual_screen_bounds();
    MoveWindow(
      window_handle_, bounds.left, bounds.top, bounds.right - bounds.left,
      bounds.bottom - bounds.top, FALSE
    );
    ShowWindow(window_handle_, SW_SHOWNOACTIVATE);
  }

  void set_alpha(float alpha) override {
    const auto alpha_byte = static_cast<BYTE>(alpha * 255.0F + 0.5F);
    SetLayeredWindowAttributes(window_handle_, RGB(0, 0, 0), alpha_byte, LWA_ALPHA);
  }

  void hide() override { ShowWindow(window_handle_, SW_HIDE); }

private:
  /// The bounding rectangle of the entire virtual desktop (all monitors).
  static RECT virtual_screen_bounds() {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return RECT{
      left, top, left + GetSystemMetrics(SM_CXVIRTUALSCREEN),
      top + GetSystemMetrics(SM_CYVIRTUALSCREEN)
    };
  }

  HWND window_handle_{nullptr};
};

/// Shell_NotifyIcon tray icon with a context menu.
class WindowsTray : public Tray {
public:
  explicit WindowsTray(TrayCallbacks callbacks) : callbacks_(std::move(callbacks)) {
    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = &WindowsTray::window_procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = tray_class;
    RegisterClassW(&window_class);

    // A hidden top-level window (never shown) rather than a message-only
    // window, so SetForegroundWindow works and the tray menu dismisses
    // correctly when the user clicks elsewhere.
    window_handle_ = CreateWindowExW(
      0, tray_class, L"Iris", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
      GetModuleHandleW(nullptr), this
    );
    if (window_handle_ == nullptr) {
      throw std::runtime_error("Failed to create tray message window");
    }

    notify_icon_data_.cbSize = sizeof(notify_icon_data_);
    notify_icon_data_.hWnd = window_handle_;
    notify_icon_data_.uID = tray_icon_id;
    notify_icon_data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notify_icon_data_.uCallbackMessage = tray_callback_message;
    notify_icon_data_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    update_tip();
    Shell_NotifyIconW(NIM_ADD, &notify_icon_data_);
  }

  ~WindowsTray() override {
    Shell_NotifyIconW(NIM_DELETE, &notify_icon_data_);
    if (window_handle_ != nullptr) {
      DestroyWindow(window_handle_);
    }
  }

  WindowsTray(const WindowsTray &) = delete;
  WindowsTray &operator=(const WindowsTray &) = delete;

  void set_enabled(bool enabled) override {
    enabled_ = enabled;
    update_tip();
  }

  void set_interval_minutes(int minutes) override {
    interval_minutes_ = minutes;
    update_tip();
  }

private:
  // NOLINTNEXTLINE(readability-identifier-naming) - CALLBACK is a Win32 macro.
  static LRESULT CALLBACK
  window_procedure(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_NCCREATE) {
      auto *creation_info = reinterpret_cast<CREATESTRUCTW *>(l_param);
      SetWindowLongPtrW(
        window_handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation_info->lpCreateParams)
      );
      return DefWindowProcW(window_handle, message, w_param, l_param);
    }
    auto *self = reinterpret_cast<WindowsTray *>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));
    if (self != nullptr && self->handle(message, w_param, l_param)) {
      return 0;
    }
    return DefWindowProcW(window_handle, message, w_param, l_param);
  }

  bool handle(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case tray_callback_message:
      if (LOWORD(l_param) == WM_RBUTTONUP || LOWORD(l_param) == WM_LBUTTONUP) {
        show_menu();
      }
      return true;
    case WM_COMMAND:
      on_command(LOWORD(w_param));
      return true;
    default:
      return false;
    }
  }

  void show_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (enabled_ ? MF_CHECKED : 0), cmd_enable, L"Enabled");

    HMENU interval_menu = CreatePopupMenu();
    for (size_t i = 0; i < std::size(interval_presets); ++i) {
      wchar_t label[32];
      std::swprintf(label, std::size(label), L"%d minutes", interval_presets[i]);
      const UINT menu_flags =
        MF_STRING | (interval_presets[i] == interval_minutes_ ? MF_CHECKED : 0);
      AppendMenuW(interval_menu, menu_flags, cmd_interval_base + static_cast<UINT>(i), label);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(interval_menu), L"Interval");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, cmd_quit, L"Quit");

    POINT cursor_position;
    GetCursorPos(&cursor_position);
    // Required so the menu dismisses correctly when clicking elsewhere.
    SetForegroundWindow(window_handle_);
    TrackPopupMenu(
      menu, TPM_RIGHTBUTTON, cursor_position.x, cursor_position.y, 0, window_handle_, nullptr
    );
    DestroyMenu(menu);
  }

  void on_command(UINT command_id) {
    if (command_id == cmd_enable) {
      enabled_ = !enabled_;
      update_tip();
      if (callbacks_.on_enabled_changed) {
        callbacks_.on_enabled_changed(enabled_);
      }
    } else if (command_id == cmd_quit) {
      if (callbacks_.on_quit) {
        callbacks_.on_quit();
      }
    } else if (
      command_id >= cmd_interval_base &&
      command_id < cmd_interval_base + std::size(interval_presets)
    ) {
      interval_minutes_ = interval_presets[command_id - cmd_interval_base];
      update_tip();
      if (callbacks_.on_interval_changed) {
        callbacks_.on_interval_changed(interval_minutes_);
      }
    }
  }

  void update_tip() {
    if (enabled_) {
      std::swprintf(
        notify_icon_data_.szTip, std::size(notify_icon_data_.szTip), L"Iris - every %d min",
        interval_minutes_
      );
    } else {
      std::swprintf(
        notify_icon_data_.szTip, std::size(notify_icon_data_.szTip), L"Iris - disabled"
      );
    }
    if (window_handle_ != nullptr) {
      Shell_NotifyIconW(NIM_MODIFY, &notify_icon_data_);
    }
  }

  TrayCallbacks callbacks_;
  HWND window_handle_{nullptr};
  NOTIFYICONDATAW notify_icon_data_{};
  bool enabled_{true};
  int interval_minutes_{20};
};

std::unique_ptr<Overlay> create_overlay() { return std::make_unique<WindowsOverlay>(); }

std::unique_ptr<Tray> create_tray(const TrayCallbacks &callbacks) {
  return std::make_unique<WindowsTray>(callbacks);
}

int run_event_loop(App &application, const Config &config) {
  bool running = true;

  std::unique_ptr<Tray> tray_icon;
  if (!config.selftest) {
    TrayCallbacks callbacks;
    callbacks.on_enabled_changed = [&application, &tray_icon](bool enabled) {
      application.set_enabled(enabled);
    };
    callbacks.on_interval_changed = [&application, &tray_icon](int minutes) {
      application.set_interval_minutes(minutes);
    };
    callbacks.on_quit = [&running]() { running = false; };
    tray_icon = create_tray(callbacks);
    if (tray_icon) {
      tray_icon->set_enabled(application.enabled());
      tray_icon->set_interval_minutes(application.interval_minutes());
    }
  }

  const auto selftest_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

  MSG message;
  while (running) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        running = false;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!running) {
      break;
    }

    application.tick(std::chrono::steady_clock::now());

    if (config.selftest) {
      if (application.completed_fades() >= 1) {
        return 0;
      }
      if (std::chrono::steady_clock::now() > selftest_deadline) {
        return 1; // Timed out without completing a fade.
      }
    }

    Sleep(15); // ~60 fps frame pacing.
  }
  return 0;
}

} // namespace Iris
