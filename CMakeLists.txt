cmake_minimum_required(VERSION 3.21)

# Version is normally injected from the git tag at release time
# (-DIRIS_VERSION=1.2.3); it defaults to 0.0.0 for local/dev builds.
if(NOT DEFINED IRIS_VERSION)
  set(IRIS_VERSION "0.0.0")
endif()
project(iris VERSION ${IRIS_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# With MinGW, statically link the runtime so the executable is standalone and
# does not depend on libstdc++/libgcc/winpthread DLLs being on PATH.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# CPM.cmake bootstrap (not vendored: downloaded once into the build tree).
# ---------------------------------------------------------------------------
set(CPM_DOWNLOAD_VERSION 0.43.1)
set(CPM_PATH "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
# Download afresh if the cached file is missing or empty (a prior failed run).
set(_cpm_needs_download TRUE)
if(EXISTS "${CPM_PATH}")
  file(SIZE "${CPM_PATH}" _cpm_size)
  if(_cpm_size GREATER 0)
    set(_cpm_needs_download FALSE)
  endif()
endif()
if(_cpm_needs_download)
  set(_cpm_url
    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake")
  message(STATUS "Downloading CPM.cmake v${CPM_DOWNLOAD_VERSION}")
  # Prefer a TLS-verified download. Fall back to unverified only if the local
  # environment has no CA certificate store (CI runners are always verified).
  file(DOWNLOAD "${_cpm_url}" "${CPM_PATH}" TLS_VERIFY ON STATUS _cpm_status)
  list(GET _cpm_status 0 _cpm_code)
  if(NOT _cpm_code EQUAL 0)
    list(GET _cpm_status 1 _cpm_error)
    message(WARNING
      "Verified download of CPM.cmake failed (${_cpm_error}); "
      "retrying without TLS verification.")
    file(DOWNLOAD "${_cpm_url}" "${CPM_PATH}" TLS_VERIFY OFF STATUS _cpm_status)
    list(GET _cpm_status 0 _cpm_code)
    if(NOT _cpm_code EQUAL 0)
      list(GET _cpm_status 1 _cpm_error)
      file(REMOVE "${CPM_PATH}")
      message(FATAL_ERROR "Failed to download CPM.cmake: ${_cpm_error}")
    endif()
  endif()
endif()
include("${CPM_PATH}")

# ---------------------------------------------------------------------------
# Platform-independent core (unit-tested, no OS dependencies).
# ---------------------------------------------------------------------------
add_library(iris_core STATIC
  src/config.cpp
  src/fade_controller.cpp)
target_include_directories(iris_core PUBLIC src)
target_compile_features(iris_core PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# Platform backend selection.
# ---------------------------------------------------------------------------
if(WIN32)
  set(IRIS_PLATFORM_SOURCES src/platform/windows.cpp)
elseif(APPLE)
  enable_language(OBJCXX)
  set(IRIS_PLATFORM_SOURCES src/platform/macos.mm)
else()
  set(IRIS_PLATFORM_SOURCES src/platform/linux.cpp)
endif()

# ---------------------------------------------------------------------------
# Executable.
# ---------------------------------------------------------------------------
if(WIN32)
  add_executable(iris WIN32 src/main.cpp src/app.cpp ${IRIS_PLATFORM_SOURCES})
  target_link_libraries(iris PRIVATE user32 gdi32 shell32)
elseif(APPLE)
  add_executable(iris MACOSX_BUNDLE src/main.cpp src/app.cpp
    ${IRIS_PLATFORM_SOURCES})
  set_target_properties(iris PROPERTIES
    # Produce Iris.app (matching the display name) rather than iris.app, so the
    # bundle path is stable for the .dmg, Homebrew cask, and LaunchAgent.
    OUTPUT_NAME "Iris"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "Iris"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.twentyware.iris"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
  target_link_libraries(iris PRIVATE
    "-framework Cocoa"
    "-framework QuartzCore")
else()
  add_executable(iris src/main.cpp src/app.cpp ${IRIS_PLATFORM_SOURCES})
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  pkg_check_modules(APPINDICATOR REQUIRED IMPORTED_TARGET
    ayatana-appindicator3-0.1)
  pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
  pkg_check_modules(XFIXES REQUIRED IMPORTED_TARGET xfixes)
  target_link_libraries(iris PRIVATE
    PkgConfig::GTK3
    PkgConfig::APPINDICATOR
    PkgConfig::X11
    PkgConfig::XFIXES)
endif()

target_link_libraries(iris PRIVATE iris_core)

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
enable_testing()

# doctest is header-only; download it but skip its (old) CMake project.
CPMAddPackage(
  NAME doctest
  GITHUB_REPOSITORY doctest/doctest
  VERSION 2.4.11
  DOWNLOAD_ONLY YES)

add_executable(iris_tests tests/test_fade_controller.cpp)
target_link_libraries(iris_tests PRIVATE iris_core)
# SYSTEM so clang-tidy does not lint doctest's own (non-conforming) headers.
target_include_directories(iris_tests SYSTEM PRIVATE "${doctest_SOURCE_DIR}")
add_test(NAME unit COMMAND iris_tests)

# End-to-end self-test that exercises the real overlay backend and exits after
# one accelerated fade. On Linux it needs a display (CI wraps ctest in xvfb).
add_test(NAME selftest COMMAND iris --selftest)
set_tests_properties(selftest PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# Developer tooling: `format`, `lint`, and `fix-lint` targets.
#   cmake --build build --target format     # reformat all sources in place
#   cmake --build build --target lint       # check formatting + run clang-tidy
#   cmake --build build --target fix-lint   # apply clang-tidy auto-fixes
# Targets are only created when the corresponding tools are found.
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT_EXE NAMES clang-format)
find_program(CLANG_TIDY_EXE NAMES clang-tidy)

# All sources to format (globbed; re-run CMake if the set changes).
file(GLOB_RECURSE IRIS_FORMAT_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/*.h"
  "${CMAKE_SOURCE_DIR}/src/*.mm"
  "${CMAKE_SOURCE_DIR}/tests/*.cpp")

# Translation units to analyse with clang-tidy. Only the current platform's
# backend is present in the compile database, so only it is included.
set(IRIS_TIDY_SOURCES
  "${CMAKE_SOURCE_DIR}/src/config.cpp"
  "${CMAKE_SOURCE_DIR}/src/fade_controller.cpp"
  "${CMAKE_SOURCE_DIR}/src/app.cpp"
  "${CMAKE_SOURCE_DIR}/src/main.cpp"
  "${CMAKE_SOURCE_DIR}/tests/test_fade_controller.cpp"
  "${CMAKE_SOURCE_DIR}/${IRIS_PLATFORM_SOURCES}")

if(CLANG_FORMAT_EXE)
  add_custom_target(format
    COMMAND "${CLANG_FORMAT_EXE}" -i ${IRIS_FORMAT_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Formatting sources with clang-format"
    VERBATIM)
else()
  message(STATUS "clang-format not found: 'format' target unavailable")
endif()

if(CLANG_TIDY_EXE)
  # `lint` checks formatting (all files, host-independent) then runs clang-tidy.
  # clang-tidy exits non-zero only on diagnostics promoted to errors by
  # .clang-tidy's WarningsAsErrors (the naming convention); other checks stay
  # advisory. Only the current platform's backend is in the compile database.
  if(CLANG_FORMAT_EXE)
    add_custom_target(lint
      COMMAND "${CLANG_FORMAT_EXE}" --dry-run --Werror ${IRIS_FORMAT_SOURCES}
      COMMAND "${CLANG_TIDY_EXE}" -p "${CMAKE_BINARY_DIR}" ${IRIS_TIDY_SOURCES}
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT "Checking formatting and running clang-tidy"
      VERBATIM)
  else()
    add_custom_target(lint
      COMMAND "${CLANG_TIDY_EXE}" -p "${CMAKE_BINARY_DIR}" ${IRIS_TIDY_SOURCES}
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT "Running clang-tidy"
      VERBATIM)
  endif()

  add_custom_target(fix-lint
    COMMAND "${CLANG_TIDY_EXE}" -p "${CMAKE_BINARY_DIR}"
            --fix --fix-errors ${IRIS_TIDY_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Applying clang-tidy auto-fixes"
    VERBATIM)
else()
  message(STATUS "clang-tidy not found: 'lint' and 'fix-lint' targets unavailable")
endif()

# ---------------------------------------------------------------------------
# Installation and packaging (Linux .deb / .rpm via CPack).
#
# Windows (Inno Setup installer / portable exe) and macOS (.dmg / .app zip) are
# packaged outside CMake in CI; see packaging/ and .github/workflows/ci.yml.
# ---------------------------------------------------------------------------
if(NOT WIN32 AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS iris RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(FILES packaging/iris.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
  install(FILES LICENSE.txt
    DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME copyright)

  set(CPACK_PACKAGE_NAME "iris")
  set(CPACK_PACKAGE_VENDOR "TwentyWare")
  set(CPACK_PACKAGE_CONTACT "Ambrus Tóth <ping@ambrus.dev>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "20-20-20 eye-rest reminder that gently dims the screen")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/twentyware/iris")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_STRIP_FILES ON)

  # Debian: name files as iris_<version>_<arch>.deb. Runtime dependencies are
  # derived from the actual linkage by dpkg-shlibdeps rather than hardcoded, so
  # they stay correct across distro renames (e.g. the 24.04 time64 transition
  # that renamed libgtk-3-0 to libgtk-3-0t64).
  set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

  # RPM: dependencies come from rpm's automatic shared-library requires (enabled
  # by default), which resolve to soname provides on the target distro, so we do
  # not hardcode distro-specific package names.
  set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
  set(CPACK_RPM_PACKAGE_LICENSE "MIT")
  set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
  set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")

  include(CPack)
endif()
