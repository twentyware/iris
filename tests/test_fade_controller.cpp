#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>

#include "config.h"
#include "fade_controller.h"

using namespace Iris;
using namespace std::chrono;

namespace {

Config make_config() {
  Config config;
  config.interval = milliseconds(1000);
  config.fade_in = milliseconds(300);
  config.hold = milliseconds(300);
  config.fade_out = milliseconds(300);
  return config;
}

// A fixed origin so tests can build time points from millisecond offsets.
const TimePoint origin{};

TimePoint at(long long ms) { return origin + milliseconds(ms); }

} // namespace

TEST_CASE("stays idle before the interval elapses") {
  FadeController controller(make_config(), at(0));

  auto state = controller.update(at(500));
  CHECK(state.overlay_visible == false);
  CHECK(state.alpha == doctest::Approx(0.0F));
  CHECK(controller.phase() == Phase::Idle);

  state = controller.update(at(999));
  CHECK(state.overlay_visible == false);
}

TEST_CASE("fade-in ramps alpha from 0 to 1") {
  FadeController controller(make_config(), at(0));

  // Enter fade-in just after the interval.
  auto state = controller.update(at(1000));
  CHECK(controller.phase() == Phase::FadeIn);
  CHECK(state.overlay_visible == true);
  CHECK(state.alpha == doctest::Approx(0.0F));

  // Halfway through the 300ms fade-in.
  state = controller.update(at(1150));
  CHECK(state.alpha == doctest::Approx(0.5F).epsilon(0.02));

  // End of fade-in reaches full black.
  state = controller.update(at(1300));
  CHECK(state.alpha == doctest::Approx(1.0F));
}

TEST_CASE("holds full black during the hold phase") {
  FadeController controller(make_config(), at(0));
  controller.update(at(1000));              // fade-in
  auto state = controller.update(at(1450)); // 150ms into hold
  CHECK(controller.phase() == Phase::Hold);
  CHECK(state.overlay_visible == true);
  CHECK(state.alpha == doctest::Approx(1.0F));
}

TEST_CASE("fade-out ramps alpha from 1 back to 0 and returns to idle") {
  FadeController controller(make_config(), at(0));
  controller.update(at(1000));

  // Middle of fade-out (interval 1000 + in 300 + hold 300 = 1600 start).
  auto state = controller.update(at(1750));
  CHECK(controller.phase() == Phase::FadeOut);
  CHECK(state.alpha == doctest::Approx(0.5F).epsilon(0.02));

  // After the full cycle we are idle again and a fade has completed.
  state = controller.update(at(1900));
  CHECK(controller.phase() == Phase::Idle);
  CHECK(state.overlay_visible == false);
  CHECK(controller.completed_fades() == 1);
}

TEST_CASE("a single coarse update can cross multiple phase boundaries") {
  FadeController controller(make_config(), at(0));
  // Jump far past a full cycle in one step.
  auto state = controller.update(at(5000));
  CHECK(controller.completed_fades() >= 1);
  // 5000ms: cycle length is 1900ms. 5000 = 1900*2 + 1200. Second cycle's idle
  // ends at 3800; +1000 interval = 4800 -> fade-in started, 200ms in.
  CHECK(controller.phase() == Phase::FadeIn);
  CHECK(state.overlay_visible == true);
}

TEST_CASE("disabling hides the overlay and resets the timer") {
  FadeController controller(make_config(), at(0));

  controller.set_enabled(false, at(900));
  auto state = controller.update(at(2000));
  CHECK(state.overlay_visible == false);
  CHECK(controller.phase() == Phase::Idle);

  // Re-enable: the interval must restart, so no fade at +500ms...
  controller.set_enabled(true, at(2000));
  state = controller.update(at(2500));
  CHECK(state.overlay_visible == false);

  // ...but a fade after a full fresh interval.
  state = controller.update(at(3050));
  CHECK(state.overlay_visible == true);
  CHECK(controller.phase() == Phase::FadeIn);
}

TEST_CASE("changing the interval while idle takes effect immediately") {
  FadeController controller(make_config(), at(0));
  controller.update(at(100));

  controller.set_interval(milliseconds(2000), at(100));
  CHECK(controller.interval() == milliseconds(2000));

  // Old interval (1000) would have fired; the new one (2000) should not yet.
  auto state = controller.update(at(1500));
  CHECK(state.overlay_visible == false);

  state = controller.update(at(2150));
  CHECK(state.overlay_visible == true);
}

TEST_CASE("config parses selftest and interval overrides") {
  auto config = Config::load({"--selftest"});
  CHECK(config.selftest == true);
  CHECK(config.interval < seconds(5));

  config = Config::load({"--interval-seconds", "42"});
  CHECK(config.selftest == false);
  CHECK(config.interval == seconds(42));

  // Invalid values are ignored, leaving the default.
  config = Config::load({"--interval-seconds", "oops"});
  CHECK(config.interval == minutes(20));
}
