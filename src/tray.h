#pragma once

#include <functional>
#include <memory>

namespace Iris {

/// Callbacks invoked by the tray menu on the UI thread.
struct TrayCallbacks {
  std::function<void(bool enabled)> on_enabled_changed;
  std::function<void(int minutes)> on_interval_changed;
  std::function<void()> on_quit;
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
  virtual void set_enabled(bool enabled) = 0;

  /// Reflects the selected interval in the menu and tooltip.
  virtual void set_interval_minutes(int minutes) = 0;
};

/// Creates the platform tray icon. Defined in the platform backend.
/// Throws std::runtime_error if the tray cannot be created.
std::unique_ptr<Tray> create_tray(const TrayCallbacks &callbacks);

/// The interval presets offered in the tray menu, in minutes.
inline constexpr int interval_presets[] = {15, 20, 30, 45, 60};

} // namespace Iris
