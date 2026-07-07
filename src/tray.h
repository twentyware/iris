#pragma once

#include <functional>
#include <memory>

namespace iris {

/// Callbacks invoked by the tray menu on the UI thread.
struct TrayCallbacks {
  std::function<void(bool enabled)> onEnabledChanged;
  std::function<void(int minutes)> onIntervalChanged;
  std::function<void()> onQuit;
};

/// A system-tray / status-bar icon with a small control menu
/// (Enable/Disable, interval presets, Quit).
///
/// Implementations are per-OS (Shell_NotifyIcon, NSStatusBar, Ayatana
/// AppIndicator). Threading: all methods must be called on the UI thread.
class Tray {
 public:
  virtual ~Tray() = default;

  /// Reflects the enabled state in the menu (checkmark) and tooltip.
  virtual void setEnabled(bool enabled) = 0;

  /// Reflects the selected interval in the menu and tooltip.
  virtual void setIntervalMinutes(int minutes) = 0;
};

/// Creates the platform tray icon. Defined in the platform backend.
/// Throws std::runtime_error if the tray cannot be created.
std::unique_ptr<Tray> createTray(const TrayCallbacks& callbacks);

/// The interval presets offered in the tray menu, in minutes.
inline constexpr int kIntervalPresets[] = {15, 20, 30, 45, 60};

}  // namespace iris
