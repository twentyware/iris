#include "config.h"

#include <algorithm>
#include <cstdlib>

namespace Iris {

namespace {

/// Parses a positive integer into `result`; returns false on any error.
bool parse_positive_int(const std::string &text, long &result) {
  if (text.empty()) {
    return false;
  }
  char *parse_end = nullptr;
  const long parsed_number = std::strtol(text.c_str(), &parse_end, 10);
  if (parse_end == text.c_str() || *parse_end != '\0' || parsed_number <= 0) {
    return false;
  }
  result = parsed_number;
  return true;
}

} // namespace

Config Config::load(const std::vector<std::string> &arguments) {
  Config config;

  // Environment override.
  if (const char *environment_value = std::getenv("IRIS_INTERVAL_SECONDS")) {
    long seconds = 0;
    if (parse_positive_int(environment_value, seconds)) {
      config.interval = std::chrono::seconds(seconds);
    }
  }

  // Command-line overrides.
  for (size_t i = 0; i < arguments.size(); ++i) {
    const std::string &argument = arguments[i];
    if (argument == "--selftest") {
      config.selftest = true;
    } else if (argument == "--interval-seconds" && i + 1 < arguments.size()) {
      long seconds = 0;
      if (parse_positive_int(arguments[++i], seconds)) {
        config.interval = std::chrono::seconds(seconds);
      }
    }
  }

  // In self-test mode, run quickly and deterministically so CI does not wait.
  if (config.selftest) {
    config.interval = std::chrono::milliseconds(400);
    config.fade_in = std::chrono::milliseconds(120);
    config.hold = std::chrono::milliseconds(80);
    config.fade_out = std::chrono::milliseconds(120);
  }

  return config;
}

} // namespace Iris
