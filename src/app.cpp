#include "app.h"

#include <chrono>
#include <cstdio>
#include <exception>

namespace Iris {

App::App(Overlay &screen_overlay, const Config &config)
    : screen_overlay_(screen_overlay), controller_(config, Clock::now()) {}

void App::tick(TimePoint now) {
  const FadeState state = controller_.update(now);
  if (state.overlay_visible) {
    if (!overlay_shown_) {
      screen_overlay_.show();
      overlay_shown_ = true;
    }
    screen_overlay_.set_alpha(state.alpha);
  } else if (overlay_shown_) {
    screen_overlay_.hide();
    overlay_shown_ = false;
  }
}

void App::set_enabled(bool enabled) { controller_.set_enabled(enabled, Clock::now()); }

void App::set_interval_minutes(int minutes) {
  controller_.set_interval(std::chrono::minutes(minutes), Clock::now());
}

int App::interval_minutes() const {
  return static_cast<int>(
    std::chrono::duration_cast<std::chrono::minutes>(controller_.interval()).count()
  );
}

int iris_run(const std::vector<std::string> &arguments) {
  try {
    const Config config = Config::load(arguments);
    auto screen_overlay = create_overlay();
    App application(*screen_overlay, config);
    return run_event_loop(application, config);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "iris: fatal error: %s\n", e.what());
    return 1;
  }
}

} // namespace Iris
