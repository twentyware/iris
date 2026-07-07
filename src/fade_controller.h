#pragma once

#include <chrono>

#include "config.h"

namespace Iris {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

/// The phase of the fade cycle.
enum class Phase { Idle, FadeIn, Hold, FadeOut };

/// What the overlay should display right now.
struct FadeState {
  bool overlay_visible{false};
  float alpha{0.0F}; // [0, 1]
};

/// Pure, OS-independent state machine driving the fade cycle.
///
/// The cycle is: Idle (for `interval`) -> FadeIn -> Hold -> FadeOut -> Idle ...
/// It is advanced by calling `update(now)` with a monotonic time point; the
/// controller performs no timing or I/O of its own, which makes it fully
/// deterministic and unit-testable. `update` tolerates coarse or irregular call
/// cadence: a single call may cross multiple phase boundaries.
///
/// Disabling resets the cycle so that re-enabling waits a full interval before
/// the next fade, matching a user's expectation after toggling the application off.
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

  /// Number of fully completed fade cycles (incremented on FadeOut -> Idle).
  /// Used by the self-test to exit after one cycle.
  int completed_fades() const { return completed_fades_; }

private:
  std::chrono::milliseconds phase_duration(Phase phase) const;

  std::chrono::milliseconds interval_;
  std::chrono::milliseconds fade_in_;
  std::chrono::milliseconds hold_;
  std::chrono::milliseconds fade_out_;

  bool enabled_{true};
  Phase phase_{Phase::Idle};
  TimePoint phase_start_;
  int completed_fades_{0};
};

} // namespace Iris
