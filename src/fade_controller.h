#pragma once

#include <chrono>

#include "config.h"

namespace Iris {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

/// The phase of the reminder cycle.
///
/// A reminder is a burst of one or more blinks. Each blink runs FadeIn -> Hold
/// -> FadeOut; consecutive blinks are separated by a transparent Gap.
enum class Phase { Idle, FadeIn, Hold, FadeOut, Gap };

/// What the overlay should display right now.
struct FadeState {
  bool overlay_visible{false};
  float alpha{0.0F}; // [0, 1]
};

/// Pure, OS-independent state machine driving the reminder cycle.
///
/// The cycle is: Idle (for `interval`) -> [FadeIn -> Hold -> FadeOut] repeated
/// `blinks` times, with a Gap between blinks -> Idle ... A blink ramps opacity
/// from 0 up to `peak_alpha` and back, so the screen dims rather than blacking
/// out. It is advanced by calling `update(now)` with a monotonic time point;
/// the controller performs no timing or I/O of its own, which makes it fully
/// deterministic and unit-testable. `update` tolerates coarse or irregular call
/// cadence: a single call may cross multiple phase boundaries.
///
/// Disabling resets the cycle so that re-enabling waits a full interval before
/// the next reminder, matching a user's expectation after toggling the
/// application off.
///
/// - Invariant: `peak_alpha` is in [0, 1] and `blinks >= 1`.
class FadeController {
public:
  FadeController(const Config &config, TimePoint start);

  /// Advances the state machine to `now` and returns the overlay state.
  ///
  /// - Precondition: `now` should be non-decreasing across calls.
  FadeState update(TimePoint now);

  void set_enabled(bool enabled, TimePoint now);
  void set_interval(std::chrono::milliseconds interval, TimePoint now);

  bool enabled() const { return enabled_; }
  Phase phase() const { return phase_; }
  std::chrono::milliseconds interval() const { return interval_; }

  /// Number of fully completed reminder cycles (incremented when the last
  /// blink's FadeOut returns to Idle). Used by the self-test to exit after one
  /// reminder.
  int completed_fades() const { return completed_fades_; }

private:
  std::chrono::milliseconds phase_duration(Phase phase) const;

  /// Advances `phase_` to the phase that follows a just-completed one,
  /// tracking blink progress and completed reminders.
  void advance_phase();

  std::chrono::milliseconds interval_;
  std::chrono::milliseconds fade_in_;
  std::chrono::milliseconds hold_;
  std::chrono::milliseconds fade_out_;
  std::chrono::milliseconds blink_gap_;
  int blinks_;
  float peak_alpha_;

  bool enabled_{true};
  Phase phase_{Phase::Idle};
  TimePoint phase_start_;
  int blink_index_{0}; // completed blinks in the current reminder
  int completed_fades_{0};
};

} // namespace Iris
