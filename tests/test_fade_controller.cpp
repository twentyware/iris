#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>

#include "config.h"
#include "fade_controller.h"

using namespace Iris;
using namespace std::chrono;

namespace {

// Round millisecond numbers keep the expected phase timeline easy to read.
// Peak opacity is deliberately < 1 so blinks dim rather than black out.
//
// Timeline of one full reminder cycle (two blinks):
//   0.....1000  Idle
//   1000..1300  blink 1 FadeIn   (0 -> peak)
//   1300..1600  blink 1 Hold     (peak)
//   1600..1900  blink 1 FadeOut  (peak -> 0)
//   1900..2100  Gap              (transparent)
//   2100..2400  blink 2 FadeIn
//   2400..2700  blink 2 Hold
//   2700..3000  blink 2 FadeOut  -> Idle, completed_fades == 1
Config make_config() {
  Config config;
  config.interval = milliseconds(1000);
  config.fade_in = milliseconds(300);
  config.hold = milliseconds(300);
  config.fade_out = milliseconds(300);
  config.blink_gap = milliseconds(200);
  config.blinks = 2;
  config.peak_alpha = 0.15F;
  return config;
}

constexpr float peak = 0.15F;

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

TEST_CASE("a blink dims to the peak, not to full black") {
  FadeController controller(make_config(), at(0));

  // Enter the first blink's fade-in just after the interval.
  auto state = controller.update(at(1000));
  CHECK(controller.phase() == Phase::FadeIn);
  CHECK(state.overlay_visible == true);
  CHECK(state.alpha == doctest::Approx(0.0F));

  // Halfway through the 300ms fade-in: half of the peak.
  state = controller.update(at(1150));
  CHECK(state.alpha == doctest::Approx(peak * 0.5F).epsilon(0.02));

  // Hold sits exactly at the peak - never fully black.
  state = controller.update(at(1450));
  CHECK(controller.phase() == Phase::Hold);
  CHECK(state.alpha == doctest::Approx(peak));

  // Fade-out ramps the peak back down.
  state = controller.update(at(1750));
  CHECK(controller.phase() == Phase::FadeOut);
  CHECK(state.alpha == doctest::Approx(peak * 0.5F).epsilon(0.02));
}

TEST_CASE("blinks twice, separated by a transparent gap, per reminder") {
  FadeController controller(make_config(), at(0));

  // After the first blink's fade-out we are in the Gap: still mapped, but fully
  // transparent, and no reminder has completed yet.
  auto state = controller.update(at(2000));
  CHECK(controller.phase() == Phase::Gap);
  CHECK(state.overlay_visible == true);
  CHECK(state.alpha == doctest::Approx(0.0F));
  CHECK(controller.completed_fades() == 0);

  // The second blink runs the same ramp.
  state = controller.update(at(2250));
  CHECK(controller.phase() == Phase::FadeIn);
  CHECK(state.alpha == doctest::Approx(peak * 0.5F).epsilon(0.02));

  state = controller.update(at(2550));
  CHECK(controller.phase() == Phase::Hold);
  CHECK(state.alpha == doctest::Approx(peak));

  // Only after the second blink does the reminder complete and return to idle.
  state = controller.update(at(3000));
  CHECK(controller.phase() == Phase::Idle);
  CHECK(state.overlay_visible == false);
  CHECK(controller.completed_fades() == 1);
}

TEST_CASE("sampling a whole reminder shows exactly two distinct blinks") {
  FadeController controller(make_config(), at(0));

  // Walk the cycle in fine steps and count rising edges from transparent to
  // visibly dimmed. There must be exactly `blinks` (2) of them.
  int blinks = 0;
  bool dimmed = false;
  float peak_seen = 0.0F;
  for (long long ms = 1000; ms <= 3000; ms += 10) {
    const auto state = controller.update(at(ms));
    peak_seen = std::max(peak_seen, state.alpha);
    const bool now_dimmed = state.alpha > 0.01F;
    if (now_dimmed && !dimmed) {
      ++blinks;
    }
    dimmed = now_dimmed;
  }
  CHECK(blinks == 2);
  // The overlay never gets darker than the configured peak.
  CHECK(peak_seen <= doctest::Approx(peak));
  CHECK(peak_seen == doctest::Approx(peak).epsilon(0.02));
}

TEST_CASE("a single coarse update can cross multiple phase boundaries") {
  FadeController controller(make_config(), at(0));
  // Jump far past a full cycle (3000ms) in one step.
  auto state = controller.update(at(3000));
  CHECK(controller.completed_fades() == 1);
  CHECK(controller.phase() == Phase::Idle);
  CHECK(state.overlay_visible == false);

  // Two full cycles land back in idle again.
  state = controller.update(at(6000));
  CHECK(controller.completed_fades() == 2);
}

TEST_CASE("disabling hides the overlay and resets the timer") {
  FadeController controller(make_config(), at(0));

  controller.set_enabled(false, at(900));
  auto state = controller.update(at(2000));
  CHECK(state.overlay_visible == false);
  CHECK(controller.phase() == Phase::Idle);

  // Re-enable: the interval must restart, so no blink at +500ms...
  controller.set_enabled(true, at(2000));
  state = controller.update(at(2500));
  CHECK(state.overlay_visible == false);

  // ...but a blink after a full fresh interval.
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

TEST_CASE("defaults dim gently and blink more than once") {
  const Config config;
  CHECK(config.blinks >= 2);
  CHECK(config.peak_alpha > 0.0F);
  CHECK(config.peak_alpha < 1.0F);
}
