# Packaging

This directory holds everything used to build the installers, packages, and
package-manager manifests. Packaging runs automatically in CI
([`.github/workflows/ci.yml`](../.github/workflows/ci.yml)) on every build — so
the installers are always exercised — and the release workflow
([`.github/workflows/release.yml`](../.github/workflows/release.yml)) attaches
the results to the GitHub release for each `v*` tag.

## What ships per platform

| Platform | Installer | Package manager | Portable |
| --- | --- | --- | --- |
| Windows | `iris-windows-x64-setup.exe` (Inno Setup) | winget, Scoop | `iris-windows-x64.exe` (bare) |
| macOS | `iris-macos-x64.dmg` | Homebrew Cask | `iris-macos-x64.zip` (`Iris.app`) |
| Linux | `iris_<v>_amd64.deb`, `iris-<v>-1.x86_64.rpm` | AUR (`iris-bin`) | `iris-linux-x64` (bare) |

The portable builds are the bare executable, except macOS where the unit of
distribution is the `Iris.app` bundle, so it is zipped.

## Files

- `iris.desktop` — Linux launcher / autostart entry. The `.deb`/`.rpm` install it
  to `/usr/share/applications`; users copy it to `~/.config/autostart/` to run at login.
- `Info.plist.in` — macOS bundle Info.plist (filled by CMake).
- `windows/iris.iss` — Inno Setup script. Builds a per-user installer with an
  opt-in "start on login" task (writes an `HKCU\...\Run` value). Accepts
  `/DMyAppVersion`, `/DSourceExe`, `/DLicensePath`, `/DOutputDir`.
- `macos/com.twentyware.iris.plist` — LaunchAgent template for login startup.
- `scoop/iris.json`, `homebrew/iris.rb`, `winget/*.yaml`, `aur/PKGBUILD` —
  package-manager manifests. They are **templates**: the release workflow
  substitutes `@VERSION@` and the `@SHA256_*@` placeholders with the real
  version and checksums, and attaches the filled copies as `package-manifests.zip`.

## Installers are tested in CI

Each build packages and then smoke-tests its installer:

- **Linux** — installs the `.deb` with `apt` (resolving runtime deps), runs
  `iris --selftest` from `/usr/bin`, then removes it.
- **Fedora (`test-rpm` job)** — `dnf install`s the `.rpm` in a `fedora:latest`
  container and checks `ldd` reports no missing libraries, so a wrong/unresolvable
  RPM dependency fails CI.
- **macOS** — mounts the `.dmg` and runs `--selftest` from the app inside it.
- **Windows** — silent-installs the setup `.exe`, runs `--selftest` from the
  installed location, then silent-uninstalls.

Package-manager manifests and templates are additionally validated by the
`package-lint` job (JSON/YAML/Ruby/bash/XML/desktop-entry syntax, plus a check
that every `@PLACEHOLDER@` is one the release workflow fills), and the release
workflow generates `SHA256SUMS` over **all** assets and self-verifies it.

Runtime dependencies are not hardcoded: the `.deb` derives them via
`dpkg-shlibdeps` and the `.rpm` via rpm's automatic shared-library requires, so
they stay correct across distro package renames. The one exception is
`gtk-layer-shell` (the native Wayland overlay), which is dlopen()ed rather than
linked so automatic dependency scanners cannot see it; it is declared manually
as a `.deb` Recommends / `.rpm` Suggests / AUR optdepends.

## Updating the package-manager channels on release

The release attaches ready-to-use, checksum-filled manifests. Publishing them to
the respective channels is a manual (or separately automated) step:

- **Homebrew** — commit `manifests/homebrew/iris.rb` to the tap repo.
- **Scoop** — commit `manifests/scoop/iris.json` to the bucket repo.
- **winget** — submit `manifests/winget/*` to `microsoft/winget-pkgs`
  (e.g. with `wingetcreate`).
- **AUR** — update `PKGBUILD` in the `iris-bin` AUR repo and regenerate
  `.SRCINFO` (`makepkg --printsrcinfo > .SRCINFO`).
