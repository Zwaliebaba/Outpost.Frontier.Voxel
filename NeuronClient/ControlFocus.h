#pragma once

#include "ControlState.h"
#include "ScreenProjection.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

/// A control's identity, chosen by the screen that lays it out and stable from frame to frame.
using ControlId = std::uint32_t;
inline constexpr ControlId NO_CONTROL = 0;

/// Pointer and keyboard interaction over the controls a screen lays out each frame (ADR-023).
///
/// Each frame the screen calls BeginLayout and then Add for every control, in tab order; a later
/// control lies on top of an earlier one for the pointer. Input between frames is tested against
/// the most recent layout. A click is a press and a release on the same enabled control; a press
/// on a disabled control is not a click, and reports a refusal instead, so the screen can show the
/// reason. Keyboard focus moves over enabled controls only, and Activate clicks the focused one.
/// Nothing here knows what a control does: the game reads clicks and supplies the states.
class ControlFocus
{
public:
  /// Forgets last frame's controls. Press and focus are kept by id, so they survive the re-layout;
  /// one whose control is not added again simply matches nothing.
  void BeginLayout();

  /// Registers a control for this frame. _reason is why it is disabled, shown without hover.
  void Add(ControlId _id, const ScreenRect& _rect, bool _enabled = true, std::string_view _reason = {});

  void PointerMove(float _x, float _y);
  /// The primary button went down at the pointer's position. Returns whether a control took it,
  /// so the game can keep the press out of the world.
  bool PointerDown();
  /// The primary button went up. Completes a click when it is over the control it pressed.
  void PointerUp();
  /// The pointer left the window, or the screen stopped tracking it: no hover, no press.
  void PointerLeave();

  void FocusNext();
  void FocusPrevious();
  void ClearFocus() noexcept
  {
    m_focused = NO_CONTROL;
  }
  /// Enter or Space: clicks the focused control when it is enabled.
  void Activate();

  /// The control a click completed on since the last call, or NO_CONTROL.
  [[nodiscard]] ControlId TakeClick() noexcept;
  /// The disabled control a press landed on since the last call, or NO_CONTROL.
  [[nodiscard]] ControlId TakeRefusal() noexcept;

  /// The topmost control under the point, enabled or not, or NO_CONTROL.
  [[nodiscard]] ControlId HitTest(float _x, float _y) const noexcept;
  [[nodiscard]] ControlId Hovered() const noexcept;
  [[nodiscard]] ControlId Focused() const noexcept
  {
    return m_focused;
  }
  [[nodiscard]] bool IsFocused(ControlId _id) const noexcept
  {
    return _id != NO_CONTROL && m_focused == _id;
  }
  [[nodiscard]] bool IsEnabled(ControlId _id) const noexcept;
  /// The reason the control is disabled, as given to Add; empty for an enabled or unknown control.
  [[nodiscard]] std::string_view Reason(ControlId _id) const noexcept;

  /// The state to draw: a disabled control is Disabled, and the authority's Pending, Confirmed and
  /// Refused win over the pointer. Otherwise a control held down under the pointer is Pressed, and
  /// one under the pointer or holding the focus is Hover, unless the game says it is Selected.
  [[nodiscard]] ControlState StateOf(ControlId _id, ControlState _authority = ControlState::Idle) const noexcept;

private:
  struct Control
  {
    ControlId id = NO_CONTROL;
    ScreenRect rect{};
    bool enabled = true;
    std::string reason;
  };

  [[nodiscard]] const Control* Find(ControlId _id) const noexcept;
  void MoveFocus(int _step);

  std::vector<Control> m_controls;
  float m_pointerX = 0.0f;
  float m_pointerY = 0.0f;
  bool m_pointerInside = false;
  ControlId m_armed = NO_CONTROL;
  ControlId m_focused = NO_CONTROL;
  ControlId m_click = NO_CONTROL;
  ControlId m_refusal = NO_CONTROL;
};

} // namespace Neuron
