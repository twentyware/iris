Looking at a computer screen for a prolonged period causes eye strain, and is a common source of fatigue and worsened
vision. The easiest way to save your eyes is to follow the 20-20-20 rule: every 20 minutes, take a 20-second break and
look at something 20 feet away (>6 meters). TwentyWare Iris reminds you to take these breaks without distrupting your
workflow. It gently fades your screen to black for a second every 20 minutes, which is enough for being reminded, but
not too distruptive, so you can finish what you are doing in a couple of seconds and then take your 20-second break.

The length of the break is up to you, you can count in your head or stop as you feel.

> Note: When looking at the screen, we tend to blink less frequently, which dries out the eyes and also contributes to
> fatigue. You can also take the opportunity to drink a sip of water during your break.

TwentyWare Iris is cross-platform and runs on Windows, Linux (X11), and macOS. It lives quietly in the
system tray / menu bar with no window of its own — the only thing you ever see is the brief fade.

## Installation

Download the latest standalone executable for your platform from the
[releases page](https://github.com/twentyware/iris/releases):

- **Windows** — `iris-windows-x64.exe`. To start it on login, place a shortcut in
  `%appdata%\Microsoft\Windows\Start Menu\Programs\Startup`.
- **Linux** — `iris-linux-x64`. Requires an **Xorg session** (Wayland has no client-side overlay
  protocol; on Ubuntu choose "Ubuntu on Xorg" at the login screen). The tray icon uses AppIndicator,
  which is built into GNOME on Ubuntu. To start it on login, copy
  [`packaging/iris.desktop`](packaging/iris.desktop) to `~/.config/autostart/`.
- **macOS** — `iris-macos-x64.zip`. Unzip and move `Iris.app` to `/Applications`. Add it to
  **System Settings → General → Login Items** to start it automatically.

## Configuration

The default interval is 20 minutes. You can pick a different preset (15/20/30/45/60 minutes) or toggle
the effect on and off from the tray / menu-bar icon.

The interval can also be set at launch for testing, via the `IRIS_INTERVAL_SECONDS` environment variable
or the `--interval-seconds <n>` argument.

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

## Improve Your Vision

If you want to maintain or improve your vision (getting rid of glasses or contact lenses), I recommend checking out the
book [Vision for Life, Revised Edition: Ten Steps to Natural Eyesight Improvement](https://www.audible.co.uk/pd/Vision-for-Life-Revised-Edition-Audiobook/1623173299?source_code=ASSGB149080119000H&share_location=pdp).
Before reading this book, I didn't expect how much improvement one can achieve without any risky surgery, just with a
couple of exercises.