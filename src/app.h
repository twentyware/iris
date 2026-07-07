#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "fade_controller.h"
#include "overlay.h"

namespace iris {

/// Ties the fade timer to the overlay. Platform-independent, but its methods
/// must be called on the UI thread because they drive the overlay.
///
/// The platform backend runs the native event loop and calls `tick()` on a
/// timer; tray callbacks call `setEnabled()` / `setIntervalMinutes()`.
class App {
 public:
  App(Overlay& overlay, const Config& config);

  /// Advances the fade state to `now` and applies it to the overlay.
  void tick(TimePoint now);

  void setEnabled(bool enabled);
  void setIntervalMinutes(int minutes);

  bool enabled() const { return controller_.enabled(); }
  int intervalMinutes() const;
  int completedFades() const { return controller_.completedFades(); }

 private:
  Overlay& overlay_;
  FadeController controller_;
  bool overlayShown_{false};
};

/// Runs the platform event loop until quit (or, in self-test mode, until one
/// fade completes). Defined in the platform backend.
int runEventLoop(App& app, const Config& config);

/// Shared entry point: loads config, creates the overlay, runs the loop.
int irisRun(const std::vector<std::string>& args);

}  // namespace iris
