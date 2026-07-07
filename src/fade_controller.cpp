#include "fade_controller.h"

#include <algorithm>

namespace Iris {

namespace {

Phase next_phase(Phase phase) {
  switch (phase) {
  case Phase::Idle:
    return Phase::FadeIn;
  case Phase::FadeIn:
    return Phase::Hold;
  case Phase::Hold:
    return Phase::FadeOut;
  case Phase::FadeOut:
    return Phase::Idle;
  }
  return Phase::Idle;
}

float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }

} // namespace

FadeController::FadeController(const Config &config, TimePoint start)
    : interval_(config.interval), fade_in_(config.fade_in), hold_(config.hold),
      fade_out_(config.fade_out), phase_start_(start) {}

std::chrono::milliseconds FadeController::phase_duration(Phase phase) const {
  switch (phase) {
  case Phase::Idle:
    return interval_;
  case Phase::FadeIn:
    return fade_in_;
  case Phase::Hold:
    return hold_;
  case Phase::FadeOut:
    return fade_out_;
  }
  return interval_;
}

FadeState FadeController::update(TimePoint now) {
  if (!enabled_) {
    // Hold in Idle and keep the timer anchored to now so that re-enabling
    // starts a fresh full interval.
    phase_ = Phase::Idle;
    phase_start_ = now;
    return FadeState{false, 0.0F};
  }

  // Advance across as many completed phases as `now` has passed. This is
  // bounded because Idle always consumes `interval_` (> 0), so every cycle
  // advances phase_start_ by at least that much; a large `now` (e.g. after the
  // machine wakes from sleep) simply fast-forwards to the correct phase. The
  // only degenerate case is a zero-length cycle, which we reject up front.
  const auto cycle = interval_ + fade_in_ + hold_ + fade_out_;
  if (cycle <= std::chrono::milliseconds(0)) {
    return FadeState{false, 0.0F};
  }
  while (true) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - phase_start_);
    const auto duration = phase_duration(phase_);
    if (elapsed < duration) {
      break;
    }
    phase_start_ += duration;
    const Phase completed = phase_;
    phase_ = next_phase(phase_);
    if (completed == Phase::FadeOut) {
      ++completed_fades_;
    }
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - phase_start_);

  switch (phase_) {
  case Phase::Idle:
    return FadeState{false, 0.0F};
  case Phase::FadeIn: {
    const float progress =
      fade_in_.count() > 0 ? static_cast<float>(elapsed.count()) / fade_in_.count() : 1.0F;
    return FadeState{true, clamp01(progress)};
  }
  case Phase::Hold:
    return FadeState{true, 1.0F};
  case Phase::FadeOut: {
    const float progress =
      fade_out_.count() > 0 ? static_cast<float>(elapsed.count()) / fade_out_.count() : 1.0F;
    return FadeState{true, clamp01(1.0F - progress)};
  }
  }

  return FadeState{false, 0.0F};
}

void FadeController::set_enabled(bool enabled, TimePoint now) {
  if (enabled == enabled_) {
    return;
  }
  enabled_ = enabled;
  // Reset to a clean idle cycle on any toggle.
  phase_ = Phase::Idle;
  phase_start_ = now;
}

void FadeController::set_interval(std::chrono::milliseconds interval, TimePoint now) {
  interval_ = interval;
  // Restart the idle wait so the new interval takes effect immediately and
  // predictably rather than depending on accumulated elapsed time.
  if (phase_ == Phase::Idle) {
    phase_start_ = now;
  }
}

} // namespace Iris
