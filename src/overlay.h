#pragma once

#include <memory>

namespace Iris {

/// A full-screen black overlay whose opacity can be animated.
///
/// Implementations are per-OS and use the least intrusive native mechanism
/// available (Windows layered window, macOS screen-saver-level NSWindow, X11
/// override-redirect window). The overlay is click-through and never steals
/// focus; it is hidden between fades so nothing is visible when idle.
///
/// Threading: all methods must be called on the platform's UI thread.
class Overlay {
public:
  virtual ~Overlay() = default;

  /// Makes the overlay visible (starting at its current alpha).
  virtual void show() = 0;

  /// Sets opacity in [0, 1]; 0 is fully transparent, 1 is fully black.
  virtual void set_alpha(float alpha) = 0;

  /// Hides the overlay so nothing is drawn.
  virtual void hide() = 0;
};

/// Creates the platform overlay. Defined in the platform backend.
/// Throws std::runtime_error if the overlay cannot be created.
std::unique_ptr<Overlay> create_overlay();

} // namespace Iris
