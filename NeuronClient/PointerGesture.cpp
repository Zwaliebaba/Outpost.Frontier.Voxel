#include "PointerGesture.h"

namespace Neuron
{

void PointerGesture::Press(float _x, float _y, std::uint64_t _timeMilliseconds) noexcept
{
  m_downX = _x;
  m_downY = _y;
  m_lastX = _x;
  m_lastY = _y;
  m_downTime = _timeMilliseconds;
  m_pressed = true;
  m_dragging = false;
}

bool PointerGesture::Move(float _x, float _y, float& _outDeltaX, float& _outDeltaY) noexcept
{
  _outDeltaX = 0.0f;
  _outDeltaY = 0.0f;
  if (!m_pressed)
  {
    return false;
  }
  if (!m_dragging)
  {
    const float dx = _x - m_downX;
    const float dy = _y - m_downY;
    if (dx * dx + dy * dy <= DRAG_THRESHOLD_PIXELS * DRAG_THRESHOLD_PIXELS)
    {
      return false;
    }
    // The drag starts here, and the movement that crossed the threshold is not lost.
    m_dragging = true;
    m_lastX = m_downX;
    m_lastY = m_downY;
  }
  _outDeltaX = _x - m_lastX;
  _outDeltaY = _y - m_lastY;
  m_lastX = _x;
  m_lastY = _y;
  return true;
}

bool PointerGesture::Release(float _x, float _y, std::uint64_t _timeMilliseconds) noexcept
{
  if (!m_pressed)
  {
    return false;
  }
  float ignoredX = 0.0f;
  float ignoredY = 0.0f;
  (void)Move(_x, _y, ignoredX, ignoredY);
  const bool click = !m_dragging && _timeMilliseconds - m_downTime <= CLICK_MAX_MILLISECONDS;
  m_pressed = false;
  m_dragging = false;
  return click;
}

void PointerGesture::Cancel() noexcept
{
  m_pressed = false;
  m_dragging = false;
}

} // namespace Neuron
