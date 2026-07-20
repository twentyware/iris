# TwentyWare Iris

Looking at a computer screen for a prolonged period causes eye strain, and is a common source of fatigue and worsened
vision. The easiest way to save your eyes is to follow the 20-20-20 rule: every 20 minutes, take a 20-second break and
look at something 20 feet away (>6 meters). TwentyWare Iris reminds you to take these breaks without distrupting your
workflow. Every 20 minutes it gently dims your screen twice — to about 15%, not to black — as an unobtrusive nudge that
it is time for a break. It is enough to catch your attention without hiding your work, so you can finish your thought in
a couple of seconds and then take your 20-second break.

The length of the break is up to you, you can count in your head or stop as you feel.

> Note: When looking at the screen, we tend to blink less frequently, which dries out the eyes and also contributes to
> fatigue. You can also take the opportunity to drink a sip of water during your break.

TwentyWare Iris is cross-platform and runs on Windows, Linux, and macOS. It lives quietly in the
system tray / menu bar with no window of its own — the only thing you ever see is the brief dim, which
appears on all of your monitors at the same time.

## Installation

Every release ships an installer, a package-manager option, and a no-install portable build for each
platform. All downloads are on the [releases page](https://github.com/twentyware/iris/releases), and a
`SHA256SUMS` file there lets you verify any download.

### Windows

- **Installer (recommended)** — `iris-windows-x64-setup.exe`. It installs per-user (no admin prompt)
  and offers a **"Start Iris when I log in"** checkbox during setup.
- **winget** — `winget install TwentyWare.Iris`
- **Scoop** — `scoop install iris` (after adding the bucket that hosts the manifest)
- **Portable** — `iris-windows-x64.exe`. Just run it; nothing is installed. To start it on login,
  place a shortcut in `%appdata%\Microsoft\Windows\Start Menu\Programs\Startup`.

### Linux

Works on both **Xorg and Wayland** sessions. On Wayland, Iris uses the native layer-shell protocol
where the compositor supports it (KDE Plasma, Sway, Hyprland, …; install `gtk-layer-shell` — the
`.deb`/`.rpm` recommend it automatically); elsewhere (notably GNOME, and thus stock Ubuntu) it falls
back to XWayland, which GNOME renders above regular windows with the opacity fade intact. The tray
icon uses AppIndicator, which is built into GNOME on Ubuntu.

- **Debian / Ubuntu** — `sudo apt install ./iris_<version>_amd64.deb` (installs `iris` and a desktop
  entry, and pulls in the required libraries).
- **Fedora / RHEL / openSUSE** — `sudo dnf install ./iris-<version>-1.x86_64.rpm`
- **Arch (AUR)** — install `iris-bin`.
- **Portable** — `iris-linux-x64`. `chmod +x iris-linux-x64 && ./iris-linux-x64`.

To start Iris on login, copy [`packaging/iris.desktop`](packaging/iris.desktop) to
`~/.config/autostart/` (the `.deb`/`.rpm` install it to `/usr/share/applications`, so you can also copy
it from there).

### macOS

- **Installer** — `iris-macos-x64.dmg`. Open it and drag **Iris** into **Applications**.
- **Homebrew** — `brew install --cask iris`
- **Portable** — `iris-macos-x64.zip`. Unzip and run `Iris.app` from anywhere.

To start Iris on login, add it under **System Settings → General → Login Items**. Alternatively, the
release includes a `com.twentyware.iris.plist` LaunchAgent (in `package-manifests.zip`, also at
[`packaging/macos/`](packaging/macos/com.twentyware.iris.plist)) — copy it to `~/Library/LaunchAgents/`
and run `launchctl load ~/Library/LaunchAgents/com.twentyware.iris.plist`.

## Configuration

The default interval is 20 minutes. You can pick a different preset (15/20/30/45/60 minutes) or toggle
the effect on and off from the tray / menu-bar icon.

The interval can also be set at launch for testing, via the `IRIS_INTERVAL_SECONDS` environment variable
or the `--interval-seconds <n>` argument.

## Building from source

Iris builds with CMake. See [CONTRIBUTING.md](CONTRIBUTING.md) for prerequisites, build steps, and
developer tooling.

## Improve Your Vision

If you want to maintain or improve your vision (getting rid of glasses or contact lenses), I recommend checking out the
book [Vision for Life, Revised Edition: Ten Steps to Natural Eyesight Improvement](https://www.audible.co.uk/pd/Vision-for-Life-Revised-Edition-Audiobook/1623173299?source_code=ASSGB149080119000H&share_location=pdp).
Before reading this book, I didn't expect how much improvement one can achieve without any risky surgery, just with a
couple of exercises.
