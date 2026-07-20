# Contributing to TwentyWare Iris

Thanks for your interest in improving Iris! This document covers how to build the project from source,
run the tests, and keep the code consistent with the house style.

## Building from source

Iris uses CMake with [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) (downloaded automatically at
configure time — nothing is vendored). On Linux, install the development packages first:

```sh
sudo apt-get install -y libgtk-3-dev libayatana-appindicator3-dev libx11-dev libxfixes-dev pkg-config
```

Then, on any platform:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The test suite has two parts: fast unit tests for the platform-independent core, and an end-to-end
`selftest` that drives the real overlay backend through one accelerated reminder and exits. On Linux the
selftest needs a display; CI wraps `ctest` in `xvfb`.

On Linux you can also produce the distributable packages locally:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DIRIS_VERSION=1.2.3
cmake --build build
( cd build && cpack -G DEB && cpack -G RPM )   # rpm needs rpmbuild installed
```

See [`packaging/README.md`](packaging/README.md) for how installers and package-manager manifests are
built and tested for every platform.

## Project layout

Iris keeps all the timing logic in a small, OS-independent core so it can be unit-tested without a
display, and confines every platform dependency to a single backend file:

- `src/fade_controller.*` — the pure state machine that turns elapsed time into an overlay opacity. It
  drives the reminder cycle: idle, then two gentle blinks to a configurable peak opacity, separated by a
  short transparent gap. Fully deterministic and unit-tested.
- `src/config.*` — baked-in defaults plus command-line / environment parsing.
- `src/app.*` — ties the controller to the overlay; platform-independent but must run on the UI thread.
- `src/overlay.h`, `src/tray.h` — the abstract interfaces each backend implements.
- `src/platform/{linux,windows,macos}.*` — the per-OS overlay, tray, and event loop. Each creates an
  overlay that covers **all** displays so a reminder appears everywhere at once, and re-fits itself to
  the current screen layout on every reminder so resolution or monitor changes are handled gracefully.
  The Linux backend has two overlay implementations selected at runtime (overridable with
  `IRIS_BACKEND=x11|wayland`): a native Wayland layer-shell surface via a dlopen()ed gtk-layer-shell,
  and an X11 override-redirect window used on Xorg and, via XWayland, on compositors without
  layer-shell (GNOME).
- `tests/` — doctest-based unit tests for the core.

## Formatting and linting

The code follows a single convention, enforced by `clang-format` and `clang-tidy`
(`UpperCamelCase` for namespaces and types, `snake_case` for functions, methods,
and variables). When `clang-format` and `clang-tidy` are installed, CMake exposes:

```sh
cmake --build build --target format     # reformat every source file in place
cmake --build build --target lint       # check formatting and run clang-tidy
cmake --build build --target fix-lint   # apply clang-tidy's auto-fixes
```

CI checks formatting on every file and enforces the naming convention. clang-format runs on every file
(including the Windows and macOS backends); clang-tidy's naming check runs on Linux, where its clang
matches the platform headers, so it covers the shared core and the Linux backend. The macOS and Windows
backends are consistently named and can be tidied locally on their own toolchains.

## Coding conventions

Iris follows a "Design by Contract" style — see
[`.github/instructions/contracts.instructions.md`](.github/instructions/contracts.instructions.md) for
the full rationale. In short:

- Document every declaration outside a function body with a summary fragment, and note preconditions,
  postconditions, and invariants that are not obvious from the summary.
- Prefer strong types and value semantics; keep mutable state encapsulated and invariants local.
- Keep the OS-independent core free of platform headers so it stays unit-testable.
