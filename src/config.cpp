#include "config.h"

#include <algorithm>
#include <cstdlib>

namespace iris {

namespace {

/// Parses a positive integer, returning std::nullopt on any error.
bool parsePositiveInt(const std::string& text, long& out) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || value <= 0) {
    return false;
  }
  out = value;
  return true;
}

}  // namespace

Config Config::load(const std::vector<std::string>& args) {
  Config config;

  // Environment override.
  if (const char* env = std::getenv("IRIS_INTERVAL_SECONDS")) {
    long seconds = 0;
    if (parsePositiveInt(env, seconds)) {
      config.interval = std::chrono::seconds(seconds);
    }
  }

  // Command-line overrides.
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--selftest") {
      config.selftest = true;
    } else if (arg == "--interval-seconds" && i + 1 < args.size()) {
      long seconds = 0;
      if (parsePositiveInt(args[++i], seconds)) {
        config.interval = std::chrono::seconds(seconds);
      }
    }
  }

  // In self-test mode, run quickly and deterministically so CI does not wait.
  if (config.selftest) {
    config.interval = std::chrono::milliseconds(400);
    config.fadeIn = std::chrono::milliseconds(120);
    config.hold = std::chrono::milliseconds(80);
    config.fadeOut = std::chrono::milliseconds(120);
  }

  return config;
}

}  // namespace iris
