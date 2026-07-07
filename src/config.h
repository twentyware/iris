#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace Iris {

/// Immutable, baked-in runtime configuration.
///
/// Defaults implement the 20-20-20 rule: a ~1 second fade to black every
/// 20 minutes. The interval can be overridden for testing via the
/// `IRIS_INTERVAL_SECONDS` environment variable or the `--interval-seconds`
/// argument, and `--selftest` switches to a fast, self-terminating run used by
/// CI to exercise the real overlay backend without waiting 20 minutes.
struct Config {
  std::chrono::milliseconds interval{std::chrono::minutes(20)};
  std::chrono::milliseconds fade_in{333};
  std::chrono::milliseconds hold{334};
  std::chrono::milliseconds fade_out{333};

  /// When true, run a single accelerated fade and exit 0. Used by CI.
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
