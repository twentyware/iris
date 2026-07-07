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

namespace iris {

namespace {

constexpr wchar_t kOverlayClass[] = L"IrisOverlayClass";
constexpr wchar_t kTrayClass[] = L"IrisTrayClass";
constexpr UINT kTrayCallbackMessage = WM_USER + 1;
constexpr UINT kTrayIconId = 1;

// Menu command ids. Interval presets occupy a contiguous range so the handler
// can map a command back to kIntervalPresets by offset.
constexpr UINT kCmdEnable = 100;
constexpr UINT kCmdQuit = 101;
constexpr UINT kCmdIntervalBase = 200;

}  // namespace

/// Layered full-screen overlay window.
class WindowsOverlay : public Overlay {
 public:
  WindowsOverlay() {
    WNDCLASSW cls = {};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = kOverlayClass;
    cls.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    RegisterClassW(&cls);

    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
            WS_EX_TOOLWINDOW,  // no taskbar button, click-through, no focus
        kOverlayClass, L"Iris Overlay", WS_POPUP, x, y, w, h, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (hwnd_ == nullptr) {
      throw std::runtime_error("Failed to create overlay window");
    }
    SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_ALPHA);
  }

  ~WindowsOverlay() override {
    if (hwnd_ != nullptr) {
      DestroyWindow(hwnd_);
    }
  }

  WindowsOverlay(const WindowsOverlay&) = delete;
  WindowsOverlay& operator=(const WindowsOverlay&) = delete;

  void show() override { ShowWindow(hwnd_, SW_SHOWNOACTIVATE); }

  void setAlpha(float alpha) override {
    const auto value = static_cast<BYTE>(alpha * 255.0F + 0.5F);
    SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), value, LWA_ALPHA);
  }

  void hide() override { ShowWindow(hwnd_, SW_HIDE); }

 private:
  HWND hwnd_{nullptr};
};

/// Shell_NotifyIcon tray icon with a context menu.
class WindowsTray : public Tray {
 public:
  explicit WindowsTray(TrayCallbacks callbacks)
      : callbacks_(std::move(callbacks)) {
    WNDCLASSW cls = {};
    cls.lpfnWndProc = &WindowsTray::wndProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = kTrayClass;
    RegisterClassW(&cls);

    // A hidden top-level window (never shown) rather than a message-only
    // window, so SetForegroundWindow works and the tray menu dismisses
    // correctly when the user clicks elsewhere.
    hwnd_ = CreateWindowExW(0, kTrayClass, L"Iris", WS_OVERLAPPED, 0, 0, 0, 0,
                            nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (hwnd_ == nullptr) {
      throw std::runtime_error("Failed to create tray message window");
    }

    icon_.cbSize = sizeof(icon_);
    icon_.hWnd = hwnd_;
    icon_.uID = kTrayIconId;
    icon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon_.uCallbackMessage = kTrayCallbackMessage;
    icon_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    updateTip();
    Shell_NotifyIconW(NIM_ADD, &icon_);
  }

  ~WindowsTray() override {
    Shell_NotifyIconW(NIM_DELETE, &icon_);
    if (hwnd_ != nullptr) {
      DestroyWindow(hwnd_);
    }
  }

  WindowsTray(const WindowsTray&) = delete;
  WindowsTray& operator=(const WindowsTray&) = delete;

  void setEnabled(bool enabled) override {
    enabled_ = enabled;
    updateTip();
  }

  void setIntervalMinutes(int minutes) override {
    intervalMinutes_ = minutes;
    updateTip();
  }

 private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam) {
    if (msg == WM_NCCREATE) {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    auto* self =
        reinterpret_cast<WindowsTray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr && self->handle(msg, wParam, lParam)) {
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  bool handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case kTrayCallbackMessage:
        if (LOWORD(lParam) == WM_RBUTTONUP ||
            LOWORD(lParam) == WM_LBUTTONUP) {
          showMenu();
        }
        return true;
      case WM_COMMAND:
        onCommand(LOWORD(wParam));
        return true;
      default:
        return false;
    }
  }

  void showMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (enabled_ ? MF_CHECKED : 0), kCmdEnable,
                L"Enabled");

    HMENU intervalMenu = CreatePopupMenu();
    for (size_t i = 0; i < std::size(kIntervalPresets); ++i) {
      wchar_t label[32];
      std::swprintf(label, std::size(label), L"%d minutes",
                    kIntervalPresets[i]);
      const UINT flags = MF_STRING | (kIntervalPresets[i] == intervalMinutes_
                                          ? MF_CHECKED
                                          : 0);
      AppendMenuW(intervalMenu, flags,
                  kCmdIntervalBase + static_cast<UINT>(i), label);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(intervalMenu),
                L"Interval");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdQuit, L"Quit");

    POINT pt;
    GetCursorPos(&pt);
    // Required so the menu dismisses correctly when clicking elsewhere.
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
  }

  void onCommand(UINT id) {
    if (id == kCmdEnable) {
      enabled_ = !enabled_;
      updateTip();
      if (callbacks_.onEnabledChanged) {
        callbacks_.onEnabledChanged(enabled_);
      }
    } else if (id == kCmdQuit) {
      if (callbacks_.onQuit) {
        callbacks_.onQuit();
      }
    } else if (id >= kCmdIntervalBase &&
               id < kCmdIntervalBase + std::size(kIntervalPresets)) {
      intervalMinutes_ = kIntervalPresets[id - kCmdIntervalBase];
      updateTip();
      if (callbacks_.onIntervalChanged) {
        callbacks_.onIntervalChanged(intervalMinutes_);
      }
    }
  }

  void updateTip() {
    if (enabled_) {
      std::swprintf(icon_.szTip, std::size(icon_.szTip),
                    L"Iris - every %d min", intervalMinutes_);
    } else {
      std::swprintf(icon_.szTip, std::size(icon_.szTip), L"Iris - disabled");
    }
    if (hwnd_ != nullptr) {
      Shell_NotifyIconW(NIM_MODIFY, &icon_);
    }
  }

  TrayCallbacks callbacks_;
  HWND hwnd_{nullptr};
  NOTIFYICONDATAW icon_{};
  bool enabled_{true};
  int intervalMinutes_{20};
};

std::unique_ptr<Overlay> createOverlay() {
  return std::make_unique<WindowsOverlay>();
}

std::unique_ptr<Tray> createTray(const TrayCallbacks& callbacks) {
  return std::make_unique<WindowsTray>(callbacks);
}

int runEventLoop(App& app, const Config& config) {
  bool running = true;

  std::unique_ptr<Tray> tray;
  if (!config.selftest) {
    TrayCallbacks callbacks;
    callbacks.onEnabledChanged = [&app, &tray](bool enabled) {
      app.setEnabled(enabled);
    };
    callbacks.onIntervalChanged = [&app, &tray](int minutes) {
      app.setIntervalMinutes(minutes);
    };
    callbacks.onQuit = [&running]() { running = false; };
    tray = createTray(callbacks);
    if (tray) {
      tray->setEnabled(app.enabled());
      tray->setIntervalMinutes(app.intervalMinutes());
    }
  }

  const auto selftestDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);

  MSG msg;
  while (running) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        running = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!running) {
      break;
    }

    app.tick(std::chrono::steady_clock::now());

    if (config.selftest) {
      if (app.completedFades() >= 1) {
        return 0;
      }
      if (std::chrono::steady_clock::now() > selftestDeadline) {
        return 1;  // Timed out without completing a fade.
      }
    }

    Sleep(15);  // ~60 fps frame pacing.
  }
  return 0;
}

}  // namespace iris
