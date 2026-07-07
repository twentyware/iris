#include <string>
#include <vector>

#include "app.h"

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

namespace {

/// Collects command-line arguments as UTF-8 strings on Windows, where the GUI
/// subsystem entry point does not receive argv directly.
std::vector<std::string> windows_arguments() {
  std::vector<std::string> arguments;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv == nullptr) {
    return arguments;
  }
  for (int i = 1; i < argc; ++i) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
    if (size > 1) {
      std::string utf8(static_cast<size_t>(size - 1), '\0');
      WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, utf8.data(), size, nullptr, nullptr);
      arguments.push_back(std::move(utf8));
    }
  }
  LocalFree(argv);
  return arguments;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - required Win32 entry point.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return Iris::iris_run(windows_arguments()); }

#else

int main(int argc, char **argv) {
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
  for (int i = 1; i < argc; ++i) {
    arguments.emplace_back(argv[i]);
  }
  return Iris::iris_run(arguments);
}

#endif
