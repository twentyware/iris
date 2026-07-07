#include "app.h"

#include <chrono>
#include <cstdio>
#include <exception>

namespace iris {

App::App(Overlay& overlay, const Config& config)
    : overlay_(overlay), controller_(config, Clock::now()) {}

void App::tick(TimePoint now) {
  const FadeState state = controller_.update(now);
  if (state.overlayVisible) {
    if (!overlayShown_) {
      overlay_.show();
      overlayShown_ = true;
    }
    overlay_.setAlpha(state.alpha);
  } else if (overlayShown_) {
    overlay_.hide();
    overlayShown_ = false;
  }
}

void App::setEnabled(bool enabled) {
  controller_.setEnabled(enabled, Clock::now());
}

void App::setIntervalMinutes(int minutes) {
  controller_.setInterval(std::chrono::minutes(minutes), Clock::now());
}

int App::intervalMinutes() const {
  return static_cast<int>(
      std::chrono::duration_cast<std::chrono::minutes>(controller_.interval())
          .count());
}

int irisRun(const std::vector<std::string>& args) {
  try {
    const Config config = Config::load(args);
    auto overlay = createOverlay();
    App app(*overlay, config);
    return runEventLoop(app, config);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "iris: fatal error: %s\n", e.what());
    return 1;
  }
}

}  // namespace iris
