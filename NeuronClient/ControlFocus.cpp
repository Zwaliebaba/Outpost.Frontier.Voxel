#include "ControlFocus.h"

namespace Neuron
{

namespace
{

[[nodiscard]] bool Contains(const ScreenRect& _rect, float _x, float _y) noexcept
{
  return _x >= _rect.left && _x < _rect.right && _y >= _rect.top && _y < _rect.bottom;
}

} // namespace

void ControlFocus::BeginLayout()
{
  m_controls.clear();
}

void ControlFocus::Add(ControlId _id, const ScreenRect& _rect, bool _enabled, std::string_view _reason)
{
  if (_id == NO_CONTROL)
  {
    return;
  }
  m_controls.push_back(Control{_id, _rect, _enabled, std::string(_reason)});
}

const ControlFocus::Control* ControlFocus::Find(ControlId _id) const noexcept
{
  if (_id == NO_CONTROL)
  {
    return nullptr;
  }
  for (const Control& control : m_controls)
  {
    if (control.id == _id)
    {
      return &control;
    }
  }
  return nullptr;
}

ControlId ControlFocus::HitTest(float _x, float _y) const noexcept
{
  for (auto control = m_controls.rbegin(); control != m_controls.rend(); ++control)
  {
    if (Contains(control->rect, _x, _y))
    {
      return control->id;
    }
  }
  return NO_CONTROL;
}

ControlId ControlFocus::Hovered() const noexcept
{
  return m_pointerInside ? HitTest(m_pointerX, m_pointerY) : NO_CONTROL;
}

bool ControlFocus::IsEnabled(ControlId _id) const noexcept
{
  const Control* control = Find(_id);
  return control != nullptr && control->enabled;
}

std::string_view ControlFocus::Reason(ControlId _id) const noexcept
{
  const Control* control = Find(_id);
  return control != nullptr && !control->enabled ? std::string_view(control->reason) : std::string_view();
}

void ControlFocus::PointerMove(float _x, float _y)
{
  m_pointerX = _x;
  m_pointerY = _y;
  m_pointerInside = true;
}

bool ControlFocus::PointerDown()
{
  const ControlId hit = Hovered();
  if (hit == NO_CONTROL)
  {
    m_armed = NO_CONTROL;
    return false;
  }
  if (IsEnabled(hit))
  {
    m_armed = hit;
  }
  else
  {
    m_armed = NO_CONTROL;
    m_refusal = hit;
  }
  return true;
}

void ControlFocus::PointerUp()
{
  if (m_armed != NO_CONTROL && Hovered() == m_armed && IsEnabled(m_armed))
  {
    m_click = m_armed;
  }
  m_armed = NO_CONTROL;
}

void ControlFocus::PointerLeave()
{
  m_pointerInside = false;
  m_armed = NO_CONTROL;
}

void ControlFocus::MoveFocus(int _step)
{
  const int count = static_cast<int>(m_controls.size());
  if (count == 0)
  {
    m_focused = NO_CONTROL;
    return;
  }
  int current = -1;
  for (int i = 0; i < count; ++i)
  {
    if (m_controls[static_cast<std::size_t>(i)].id == m_focused)
    {
      current = i;
      break;
    }
  }
  // From no focus, the first step lands on the first control going forward or the last going back.
  int index = current < 0 ? (_step > 0 ? -1 : count) : current;
  for (int tried = 0; tried < count; ++tried)
  {
    index = ((index + _step) % count + count) % count;
    const Control& candidate = m_controls[static_cast<std::size_t>(index)];
    if (candidate.enabled)
    {
      m_focused = candidate.id;
      return;
    }
  }
  m_focused = NO_CONTROL;
}

void ControlFocus::FocusNext()
{
  MoveFocus(1);
}

void ControlFocus::FocusPrevious()
{
  MoveFocus(-1);
}

void ControlFocus::Activate()
{
  if (IsEnabled(m_focused))
  {
    m_click = m_focused;
  }
  else if (Find(m_focused) != nullptr)
  {
    m_refusal = m_focused;
  }
}

ControlId ControlFocus::TakeClick() noexcept
{
  const ControlId click = m_click;
  m_click = NO_CONTROL;
  return click;
}

ControlId ControlFocus::TakeRefusal() noexcept
{
  const ControlId refusal = m_refusal;
  m_refusal = NO_CONTROL;
  return refusal;
}

ControlState ControlFocus::StateOf(ControlId _id, ControlState _authority) const noexcept
{
  const Control* control = Find(_id);
  if (control != nullptr && !control->enabled)
  {
    return ControlState::Disabled;
  }
  if (_authority == ControlState::Disabled || _authority == ControlState::Pending || _authority == ControlState::Confirmed ||
      _authority == ControlState::Refused)
  {
    return _authority;
  }
  if (control == nullptr)
  {
    return _authority;
  }
  const bool hovered = Hovered() == _id;
  if (hovered && m_armed == _id)
  {
    return ControlState::Pressed;
  }
  if (_authority == ControlState::Selected)
  {
    return ControlState::Selected;
  }
  return hovered || IsFocused(_id) ? ControlState::Hover : ControlState::Idle;
}

} // namespace Neuron
