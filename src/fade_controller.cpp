#include "fade_controller.h"

#include <algorithm>

namespace iris {

namespace {

Phase nextPhase(Phase phase) {
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

float clamp01(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

}  // namespace

FadeController::FadeController(const Config& config, TimePoint start)
    : interval_(config.interval),
      fadeIn_(config.fadeIn),
      hold_(config.hold),
      fadeOut_(config.fadeOut),
      phaseStart_(start) {}

std::chrono::milliseconds FadeController::phaseDuration(Phase phase) const {
  switch (phase) {
    case Phase::Idle:
      return interval_;
    case Phase::FadeIn:
      return fadeIn_;
    case Phase::Hold:
      return hold_;
    case Phase::FadeOut:
      return fadeOut_;
  }
  return interval_;
}

FadeState FadeController::update(TimePoint now) {
  if (!enabled_) {
    // Hold in Idle and keep the timer anchored to now so that re-enabling
    // starts a fresh full interval.
    phase_ = Phase::Idle;
    phaseStart_ = now;
    return FadeState{false, 0.0F};
  }

  // Advance across as many completed phases as `now` has passed. This is
  // bounded because Idle always consumes `interval_` (> 0), so every cycle
  // advances phaseStart_ by at least that much; a large `now` (e.g. after the
  // machine wakes from sleep) simply fast-forwards to the correct phase. The
  // only degenerate case is a zero-length cycle, which we reject up front.
  const auto cycle = interval_ + fadeIn_ + hold_ + fadeOut_;
  if (cycle <= std::chrono::milliseconds(0)) {
    return FadeState{false, 0.0F};
  }
  while (true) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - phaseStart_);
    const auto duration = phaseDuration(phase_);
    if (elapsed < duration) {
      break;
    }
    phaseStart_ += duration;
    const Phase completed = phase_;
    phase_ = nextPhase(phase_);
    if (completed == Phase::FadeOut) {
      ++completedFades_;
    }
  }

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - phaseStart_);

  switch (phase_) {
    case Phase::Idle:
      return FadeState{false, 0.0F};
    case Phase::FadeIn: {
      const float progress =
          fadeIn_.count() > 0
              ? static_cast<float>(elapsed.count()) / fadeIn_.count()
              : 1.0F;
      return FadeState{true, clamp01(progress)};
    }
    case Phase::Hold:
      return FadeState{true, 1.0F};
    case Phase::FadeOut: {
      const float progress =
          fadeOut_.count() > 0
              ? static_cast<float>(elapsed.count()) / fadeOut_.count()
              : 1.0F;
      return FadeState{true, clamp01(1.0F - progress)};
    }
  }

  return FadeState{false, 0.0F};
}

void FadeController::setEnabled(bool enabled, TimePoint now) {
  if (enabled == enabled_) {
    return;
  }
  enabled_ = enabled;
  // Reset to a clean idle cycle on any toggle.
  phase_ = Phase::Idle;
  phaseStart_ = now;
}

void FadeController::setInterval(std::chrono::milliseconds interval,
                                 TimePoint now) {
  interval_ = interval;
  // Restart the idle wait so the new interval takes effect immediately and
  // predictably rather than depending on accumulated elapsed time.
  if (phase_ == Phase::Idle) {
    phaseStart_ = now;
  }
}

}  // namespace iris
