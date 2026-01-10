// clang-format off
#include <windows.h> // relative order matters
#include <shellapi.h>
// clang-format on

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

// Undefine Windows min/max macros to avoid conflicts with std::min/std::max
#undef min
#undef max

/// Custom window message for system tray notifications.
constexpr UINT WM_TRAYICON = WM_USER + 1;

/// Menu command identifiers for the system tray context menu.
enum class MenuCommand : UINT {  // NOLINT(*-enum-size) windows.h compatibility
  EnableDisable = 1001,
  Configure = 1002,
  Exit = 1003
};

using WindowHandle = HWND;
using MenuHandle = HMENU;

/// Creates and manages a fullscreen overlay window that fades to black and
/// back.
///
/// - Invariant: windowHandle is always a valid window handle or nullptr.
/// - Postcondition: Window covers all monitors with a black overlay at alpha=0.
class OverlayWindow {
  WindowHandle windowHandle;

  const int FADE_STEPS = 60;

  /// Window procedure for handling messages.
  ///
  /// Layered windows don't require custom painting; DefWindowProc handles all
  /// messages.
  ///
  /// - Precondition: hwnd must be a valid window handle.
  static LRESULT CALLBACK HandleWindowMessage(WindowHandle hwnd,
                                              UINT uMsg,
                                              WPARAM wParam,
                                              LPARAM lParam) {
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }

  /// Sets the window's alpha transparency level.
  ///
  /// - Precondition: hwnd must be a valid layered window handle.
  void SetAlpha(const BYTE alpha) {
    // NOLINTNEXTLINE(*-signed-bitwise)
    SetLayeredWindowAttributes(windowHandle, RGB(0, 0, 0), alpha, LWA_ALPHA);
  }

  /// Runs a timed effect loop, invoking drawEffect each frame with progress
  /// [0.0, 1.0].
  ///
  /// - Template Parameter: AlphaFunc - callable type taking double progress and
  /// returning BYTE alpha.
  /// - Precondition: durationMs > 0.
  /// - Postcondition: Effect runs for the specified duration using wall-clock
  /// time.
  /// - Complexity: O(1) per frame, total frames ~= (durationMs * TARGET_FPS /
  /// 1000).
  template <typename AlphaFunc>
  void RunTimedEffect(const int durationMs, AlphaFunc drawEffect) {
    using namespace std::chrono;
    constexpr int TARGET_FPS = 60;
    constexpr int FRAME_TIME_MS = 1000 / TARGET_FPS;

    const auto startTime = steady_clock::now();
    auto lastFrameTime = startTime;

    while (true) {
      auto currentTime = steady_clock::now();
      auto elapsedMs =
          duration_cast<milliseconds>(currentTime - startTime).count();

      if (elapsedMs >= durationMs) {
        break;
      }

      // Compute progress as a value between 0.0 and 1.0
      const auto progress = static_cast<double>(elapsedMs) / durationMs;
      drawEffect(progress);

      // Frame rate limiting
      auto nextFrameTime = lastFrameTime + milliseconds(FRAME_TIME_MS);
      std::this_thread::sleep_until(nextFrameTime);
      lastFrameTime = nextFrameTime;
    }
  }

 public:
  /// Initializes the overlay window covering all monitors.
  ///
  /// - Precondition: Window class must be registered successfully.
  /// - Postcondition: Window is created and visible with alpha=0, or throws on
  /// failure.
  OverlayWindow() {
    constexpr auto CLASS_NAME = L"OverlayWindowClass";

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = HandleWindowMessage;
    windowClass.hInstance = GetModuleHandle(nullptr);
    windowClass.lpszClassName = CLASS_NAME;
    // NOLINTNEXTLINE(*-signed-bitwise)
    windowClass.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));

    RegisterClassW(&windowClass);

    // Get virtual screen dimensions to cover all monitors
    const int screenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int screenTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // Create a layered, topmost window that doesn't activate
    windowHandle = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST |  // NOLINT(*-signed-bitwise)
            WS_EX_TRANSPARENT |
            WS_EX_NOACTIVATE,  // dwExStyle: extended window styles
        CLASS_NAME,            // lpClassName: registered window class name
        L"Overlay",            // lpWindowName: window title
        WS_POPUP,              // dwStyle: borderless popup window
        screenLeft, screenTop, screenWidth,
        screenHeight,              // x, y, width, height: cover all monitors
        nullptr,                   // hWndParent: no parent window
        nullptr,                   // hMenu: no menu
        GetModuleHandle(nullptr),  // hInstance: current module handle
        nullptr                    // lpParam: no additional data
    );

    if (windowHandle == nullptr) {
      throw std::runtime_error("Failed to create overlay window");
    }

    // Start fully transparent
    SetAlpha(0);
    ShowWindow(windowHandle, SW_SHOWNOACTIVATE);
    UpdateWindow(windowHandle);
  }

  /// Performs the fade effect: fade to black, hold, fade back.
  ///
  /// - Precondition: Window must be created successfully.
  /// - Postcondition: Window returns to fully transparent state.
  /// - Complexity: O(1) per frame, duration determined by wall-clock time.
  void PerformFadeEffect() {
    // Fade to black: alpha goes from 0 to 255
    constexpr auto fullDurationMs = 1000;
    RunTimedEffect(fullDurationMs / 3, [&](double progress) {
      SetAlpha(static_cast<BYTE>(255 * progress));
    });

    // Hold black: alpha stays at 255
    RunTimedEffect(fullDurationMs / 3, [&](double) { SetAlpha(255); });

    // Fade back to transparent: alpha goes from 255 to 0
    RunTimedEffect(fullDurationMs / 3, [&](double progress) {
      SetAlpha(static_cast<BYTE>(255 * (1.0 - progress)));
    });
    // Ensure final state is fully transparent
    SetAlpha(0);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  ~OverlayWindow() {
    if (windowHandle != nullptr) {
      DestroyWindow(windowHandle);
      windowHandle = nullptr;
    }
  }
  OverlayWindow(const OverlayWindow& other) = delete;
  OverlayWindow(OverlayWindow&& other) noexcept = delete;
  OverlayWindow& operator=(const OverlayWindow& other) = delete;
  OverlayWindow& operator=(OverlayWindow&& other) noexcept = delete;
};

/// Global state for the application.
struct AppState {
  std::atomic<bool> isEnabled{true};
  std::atomic<int> intervalMinutes{20};
  std::atomic<bool> shouldExit{false};
  WindowHandle mainWindowHandle{nullptr};
  NOTIFYICONDATAW notifyIconData{};
};

AppState g_appState;

/// Updates the tray icon tooltip to reflect current state.
///
/// - Precondition: g_appState.notifyIconData must be initialized.
void UpdateTrayTooltip() {
  constexpr size_t bufferSize =
      sizeof(g_appState.notifyIconData.szTip) / sizeof(wchar_t);

  if (g_appState.isEnabled) {
    std::swprintf(g_appState.notifyIconData.szTip, bufferSize,
                  L"Iris - Enabled (Interval: %dm)",
                  g_appState.intervalMinutes.load());
  } else {
    constexpr auto disabledText = L"Iris - Disabled";
    const auto textLen = std::wcslen(disabledText);
    std::copy_n(disabledText, std::min(textLen, bufferSize),
                g_appState.notifyIconData.szTip);
    g_appState.notifyIconData.szTip[bufferSize - 1] = L'\0';
  }
  Shell_NotifyIconW(NIM_MODIFY, &g_appState.notifyIconData);
}

/// Displays a context menu for the tray icon.
///
/// - Precondition: hwnd must be a valid window handle.
void ShowTrayMenu(WindowHandle hwnd) {
  POINT cursorPos;
  GetCursorPos(&cursorPos);

  HMENU hMenu = CreatePopupMenu();
  if (hMenu != nullptr) {
    const wchar_t* enableDisableText =
        g_appState.isEnabled ? L"Disable" : L"Enable";
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(MenuCommand::EnableDisable),
                enableDisableText);
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(MenuCommand::Configure),
                L"Configure Interval...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(MenuCommand::Exit),
                L"Exit");

    // Required for popup menus to work correctly
    SetForegroundWindow(hwnd);

    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, cursorPos.x, cursorPos.y, 0, hwnd,
                   nullptr);
    DestroyMenu(hMenu);
  }
}

/// Dialog state for the configuration window.
struct ConfigDialogState {
  WindowHandle editControl;
  bool* shouldClose;  // owned by ConfigureInterval function

  explicit ConfigDialogState(WindowHandle edit_control, bool* should_close)
      : editControl(edit_control), shouldClose(should_close) {}
};

/// Dialog procedure for the configuration window.
///
/// - Precondition: lParam on WM_INITDIALOG contains pointer to
/// ConfigDialogState.
LRESULT CALLBACK ConfigDialogProc(WindowHandle hwnd,
                                  UINT msg,
                                  WPARAM wParam,
                                  LPARAM lParam) {
  // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
  auto* state = reinterpret_cast<ConfigDialogState*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
    case WM_NCCREATE:
      // Allow default processing to set the window title correctly
      return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_INITDIALOG:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);
      return TRUE;

    case WM_COMMAND: {
      if (LOWORD(wParam) == IDOK) {
        if (state && state->editControl) {
          constexpr size_t textBufferSize = 32;
          std::array<wchar_t, textBufferSize> text{};
          GetWindowTextW(state->editControl, text.data(), text.size());

          constexpr int MIN_INTERVAL = 1;
          constexpr int MAX_INTERVAL = 3 * 60;
          const auto newInterval =
              static_cast<int>(std::wcstol(text.data(), nullptr, 10));

          if (newInterval >= MIN_INTERVAL && newInterval <= MAX_INTERVAL) {
            g_appState.intervalMinutes = newInterval;
            UpdateTrayTooltip();
            if (state->shouldClose) {
              *state->shouldClose = true;
            }
            DestroyWindow(hwnd);
          } else {
            MessageBoxW(
                hwnd, L"Please enter a value between 1 and 180 minutes.",
                L"Invalid Input",
                static_cast<UINT>(MB_OK) | static_cast<UINT>(MB_ICONWARNING));
          }
        }
        return TRUE;
      }
      if (LOWORD(wParam) == IDCANCEL) {
        if (state && state->shouldClose) {
          *state->shouldClose = true;
        }
        DestroyWindow(hwnd);
        return TRUE;
      }
    } break;

    case WM_CLOSE:
      if (state && state->shouldClose) {
        *state->shouldClose = true;
      }
      DestroyWindow(hwnd);
      return TRUE;

    default:
      break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/// Shows configuration dialog for setting the interval.
///
/// - Postcondition: Updates interval if valid input provided.
void ConfigureInterval(WindowHandle hwnd) {
  constexpr size_t inputBufferSize = 32;
  std::array<wchar_t, inputBufferSize> inputText{};
  std::swprintf(inputText.data(), inputText.size(), L"%d",
                g_appState.intervalMinutes.load());

  // Register dialog window class
  const wchar_t CLASS_NAME[] = L"ConfigDialogClass";
  WNDCLASSW wc = {};
  wc.lpfnWndProc = ConfigDialogProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = CLASS_NAME;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

  RegisterClassW(&wc);  // Ignore if already registered

  // Create dialog window
  const WindowHandle hDlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, CLASS_NAME, L"Configure Interval",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT,
      CW_USEDEFAULT, 320, 150, hwnd, nullptr, GetModuleHandle(nullptr),
      nullptr);

  if (hDlg) {
    // Create static text label
    CreateWindowExW(0, L"STATIC", L"Interval (minutes):", WS_CHILD | WS_VISIBLE,
                    10, 10, 280, 20, hDlg, nullptr, GetModuleHandle(nullptr),
                    nullptr);

    // Create edit control
    constexpr int EDIT_CONTROL_ID = 100;
    WindowHandle hEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", inputText.data(),
        WS_CHILD | WS_VISIBLE | ES_NUMBER, 10, 35, 280, 25, hDlg,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(EDIT_CONTROL_ID)),
        GetModuleHandle(nullptr), nullptr);

    // Create OK button
    CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 60, 75, 80, 25,
                    hDlg, reinterpret_cast<HMENU>(static_cast<intptr_t>(IDOK)),
                    GetModuleHandle(nullptr), nullptr);

    // Create Cancel button
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE, 160, 75, 80,
                    25, hDlg,
                    reinterpret_cast<HMENU>(static_cast<intptr_t>(IDCANCEL)),
                    GetModuleHandle(nullptr), nullptr);

    // Set up dialog state
    bool shouldClose = false;
    ConfigDialogState state{
        hEdit,         // editControl
        &shouldClose,  // shouldClose
    };

    SendMessageW(hDlg, WM_INITDIALOG, 0, reinterpret_cast<LPARAM>(&state));

    // Message loop for modal dialog
    MSG msg;
    while (!shouldClose && (GetMessageW(&msg, nullptr, 0, 0) != 0)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
}

/// Window procedure for the hidden main window and tray icon.
///
/// - Precondition: hwnd must be a valid window handle.
LRESULT CALLBACK TrayWindowProc(WindowHandle hwnd,
                                UINT uMsg,
                                WPARAM wParam,
                                LPARAM lParam) {
  switch (uMsg) {
    case WM_TRAYICON:
      if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
        ShowTrayMenu(hwnd);
      }
      break;

    case WM_COMMAND: {
      const auto command = static_cast<MenuCommand>(LOWORD(wParam));
      switch (command) {
        case MenuCommand::EnableDisable:
          g_appState.isEnabled = !g_appState.isEnabled;
          UpdateTrayTooltip();
          break;

        case MenuCommand::Configure:
          ConfigureInterval(hwnd);
          break;

        case MenuCommand::Exit:
          g_appState.shouldExit = true;
          Shell_NotifyIconW(NIM_DELETE, &g_appState.notifyIconData);
          PostQuitMessage(0);
          break;
      }
      break;
    }

    case WM_DESTROY:
      Shell_NotifyIconW(NIM_DELETE, &g_appState.notifyIconData);
      PostQuitMessage(0);
      break;

    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
  return 0;
}

/// Creates the system tray icon.
///
/// - Precondition: hwnd must be a valid window handle.
/// - Postcondition: Tray icon is visible in the notification area.
void CreateTrayIcon(const WindowHandle hwnd) {
  g_appState.notifyIconData.cbSize = sizeof(NOTIFYICONDATAW);
  g_appState.notifyIconData.hWnd = hwnd;
  g_appState.notifyIconData.uID = 1;
  g_appState.notifyIconData.uFlags = static_cast<UINT>(NIF_ICON) |
                                     static_cast<UINT>(NIF_MESSAGE) |
                                     static_cast<UINT>(NIF_TIP);
  g_appState.notifyIconData.uCallbackMessage = WM_TRAYICON;

  // Load default application icon
  g_appState.notifyIconData.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

  UpdateTrayTooltip();
  Shell_NotifyIconW(NIM_ADD, &g_appState.notifyIconData);
}

/// Background thread that performs the fade effects at regular intervals.
///
/// - Precondition: g_appState must be initialized.
/// - Postcondition: Thread exits when g_appState.shouldExit is true.
void EffectThread() {
  try {
    OverlayWindow overlay;
    auto lastEffectTime = std::chrono::steady_clock::now();

    while (!g_appState.shouldExit) {
      auto currentTime = std::chrono::steady_clock::now();

      if (g_appState.isEnabled) {
        auto elapsedMinutes = std::chrono::duration_cast<std::chrono::minutes>(
                                  currentTime - lastEffectTime)
                                  .count();

        if (elapsedMinutes >= g_appState.intervalMinutes) {
          overlay.PerformFadeEffect();
          lastEffectTime = std::chrono::steady_clock::now();
        }
      } else {
        // When disabled, reset the timer so it starts counting from when
        // enabled
        lastEffectTime = std::chrono::steady_clock::now();
      }

      // Check every second
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  } catch (const std::exception& e) {
    MessageBoxA(nullptr, e.what(), "Iris: Unexpected Error",
                static_cast<UINT>(MB_OK) | static_cast<UINT>(MB_ICONERROR));
  }
}

/// Entry point for the Iris system tray application.
///
/// Creates a hidden window with a system tray icon that allows
/// enabling/disabling the fade effect and configuring the interval between
/// effects.
///
/// - Postcondition: Runs until user exits via tray menu.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  // Register window class for hidden main window
  constexpr wchar_t CLASS_NAME[] = L"TwentywareIris";

  WNDCLASSW wc = {};
  wc.lpfnWndProc = TrayWindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;

  RegisterClassW(&wc);

  // Create hidden window for message handling
  g_appState.mainWindowHandle = CreateWindowExW(
      0, CLASS_NAME, L"Iris", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr);

  if (g_appState.mainWindowHandle == nullptr) {
    return 1;
  }

  // Create system tray icon
  CreateTrayIcon(g_appState.mainWindowHandle);

  // Start effect thread
  std::thread effectThread(EffectThread);

  // Message loop
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  // Cleanup
  g_appState.shouldExit = true;
  if (effectThread.joinable()) {
    effectThread.join();
  }

  return 0;
}
