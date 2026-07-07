#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "fade_controller.h"
#include "overlay.h"

namespace Iris {

/// Ties the fade timer to the overlay. Platform-independent, but its methods
/// must be called on the UI thread because they drive the overlay.
///
/// The platform backend runs the native event loop and calls `tick()` on a
/// timer; tray callbacks call `set_enabled()` / `set_interval_minutes()`.
class App {
public:
  App(Overlay &screen_overlay, const Config &config);

  /// Advances the fade state to `now` and applies it to the overlay.
  void tick(TimePoint now);

  void set_enabled(bool enabled);
  void set_interval_minutes(int minutes);

  bool enabled() const { return controller_.enabled(); }
  int interval_minutes() const;
  int completed_fades() const { return controller_.completed_fades(); }

private:
  Overlay &screen_overlay_;
  FadeController controller_;
  bool overlay_shown_{false};
};

/// Runs the platform event loop until quit (or, in self-test mode, until one
/// fade completes). Defined in the platform backend.
int run_event_loop(App &application, const Config &config);

/// Shared entry point: loads config, creates the overlay, runs the loop.
int iris_run(const std::vector<std::string> &arguments);

} // namespace Iris
