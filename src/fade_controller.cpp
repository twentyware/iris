#include "fade_controller.h"

#include <algorithm>

namespace Iris {

namespace {

float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }

} // namespace

FadeController::FadeController(const Config &config, TimePoint start)
    : interval_(config.interval), fade_in_(config.fade_in), hold_(config.hold),
      fade_out_(config.fade_out), blink_gap_(config.blink_gap), blinks_(std::max(1, config.blinks)),
      peak_alpha_(clamp01(config.peak_alpha)), phase_start_(start) {}

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
  case Phase::Gap:
    return blink_gap_;
  }
  return interval_;
}

void FadeController::advance_phase() {
  switch (phase_) {
  case Phase::Idle:
    phase_ = Phase::FadeIn;
    return;
  case Phase::FadeIn:
    phase_ = Phase::Hold;
    return;
  case Phase::Hold:
    phase_ = Phase::FadeOut;
    return;
  case Phase::FadeOut:
    // One blink finished. Start the next one after a Gap, or end the reminder.
    ++blink_index_;
    if (blink_index_ < blinks_) {
      phase_ = Phase::Gap;
    } else {
      phase_ = Phase::Idle;
      blink_index_ = 0;
      ++completed_fades_;
    }
    return;
  case Phase::Gap:
    phase_ = Phase::FadeIn;
    return;
  }
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
  // bounded because Idle always consumes `interval_`, so every full cycle
  // advances phase_start_ by at least that much; a large `now` (e.g. after the
  // machine wakes from sleep) simply fast-forwards to the correct phase. The
  // only degenerate case is a zero-length cycle, which we reject up front.
  const auto cycle =
    interval_ + blinks_ * (fade_in_ + hold_ + fade_out_) + (blinks_ - 1) * blink_gap_;
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
    advance_phase();
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - phase_start_);

  switch (phase_) {
  case Phase::Idle:
    return FadeState{false, 0.0F};
  case Phase::FadeIn: {
    const float progress =
      fade_in_.count() > 0 ? static_cast<float>(elapsed.count()) / fade_in_.count() : 1.0F;
    return FadeState{true, peak_alpha_ * clamp01(progress)};
  }
  case Phase::Hold:
    return FadeState{true, peak_alpha_};
  case Phase::FadeOut: {
    const float progress =
      fade_out_.count() > 0 ? static_cast<float>(elapsed.count()) / fade_out_.count() : 1.0F;
    return FadeState{true, peak_alpha_ * clamp01(1.0F - progress)};
  }
  case Phase::Gap:
    // Between blinks: keep the overlay mapped but fully transparent so the two
    // blinks read as distinct without a map/unmap flicker.
    return FadeState{true, 0.0F};
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
  blink_index_ = 0;
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
