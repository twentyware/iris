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
std::vector<std::string> windowsArgs() {
  std::vector<std::string> args;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv == nullptr) {
    return args;
  }
  for (int i = 1; i < argc; ++i) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size > 1) {
      std::string utf8(static_cast<size_t>(size - 1), '\0');
      WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, utf8.data(), size, nullptr,
                          nullptr);
      args.push_back(std::move(utf8));
    }
  }
  LocalFree(argv);
  return args;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  return iris::irisRun(windowsArgs());
}

#else

int main(int argc, char** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  return iris::irisRun(args);
}

#endif
