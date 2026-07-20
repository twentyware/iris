#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace Iris {

/// Immutable, baked-in runtime configuration.
///
/// Defaults implement the 20-20-20 rule: every 20 minutes the screen gently
/// dims twice, to about 15% opacity rather than fully black, as an unobtrusive
/// reminder. The interval can be overridden for testing via the
/// `IRIS_INTERVAL_SECONDS` environment variable or the `--interval-seconds`
/// argument, and `--selftest` switches to a fast, self-terminating run used by
/// CI to exercise the real overlay backend without waiting 20 minutes.
struct Config {
  std::chrono::milliseconds interval{std::chrono::minutes(20)};
  std::chrono::milliseconds fade_in{220};
  std::chrono::milliseconds hold{90};
  std::chrono::milliseconds fade_out{220};

  /// Pause (fully transparent) between consecutive blinks in one reminder.
  std::chrono::milliseconds blink_gap{150};

  /// Number of blinks per reminder.
  /// - Invariant: `>= 1`.
  int blinks{2};

  /// Peak opacity of a blink, in [0, 1]; 0 is transparent, 1 is fully black.
  /// The default dims gently rather than blacking the screen out.
  float peak_alpha{0.15F};

  /// When true, run a single accelerated reminder and exit 0. Used by CI.
  bool selftest{false};

  /// Parses configuration from environment variables and command-line
  /// arguments. Later sources win: defaults < environment_value < arguments.
  ///
  /// - `IRIS_INTERVAL_SECONDS=<n>` sets the interval.
  /// - `--interval-seconds <n>` sets the interval.
  /// - `--selftest` enables self-test mode (and shortens the interval so a
  ///   fade happens promptly).
  static Config load(const std::vector<std::string> &arguments);
};

} // namespace Iris
