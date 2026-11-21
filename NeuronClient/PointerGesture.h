#pragma once

#include <cstdint>

namespace Neuron
{

/// Tells a click from a drag for one mouse button. A press becomes a drag once the pointer moves
/// further than DRAG_THRESHOLD_PIXELS from where it went down; a release that never became a drag,
/// and came soon enough, is a click.
class PointerGesture
{
public:
  static constexpr float DRAG_THRESHOLD_PIXELS = 5.0f;
  static constexpr std::uint64_t CLICK_MAX_MILLISECONDS = 500;

  void Press(float _x, float _y, std::uint64_t _timeMilliseconds) noexcept;

  /// Returns the movement since the previous call when dragging, and reports whether it is.
  bool Move(float _x, float _y, float& _outDeltaX, float& _outDeltaY) noexcept;

  /// Ends the gesture and reports whether it was a click.
  [[nodiscard]] bool Release(float _x, float _y, std::uint64_t _timeMilliseconds) noexcept;

  void Cancel() noexcept;

  [[nodiscard]] bool IsPressed() const noexcept
  {
    return m_pressed;
  }
  [[nodiscard]] bool IsDragging() const noexcept
  {
    return m_dragging;
  }

private:
  float m_downX = 0.0f;
  float m_downY = 0.0f;
  float m_lastX = 0.0f;
  float m_lastY = 0.0f;
  std::uint64_t m_downTime = 0;
  bool m_pressed = false;
  bool m_dragging = false;
};

} // namespace Neuron
