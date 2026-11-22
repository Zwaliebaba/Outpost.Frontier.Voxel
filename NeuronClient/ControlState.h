#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Neuron
{

/// The states every control shares (GameplayUiProposal.md, the interaction contract). Hover and
/// Pressed come from the pointer and keyboard focus (ControlFocus); the others come from the game:
/// Selected from what is chosen, Disabled with a reason, and Pending, Confirmed and Refused from
/// the authority's replies. A click is never proof that a command succeeded.
enum class ControlState : std::uint8_t
{
  Idle,
  Hover,
  Pressed,
  Selected,
  Disabled,
  Pending,
  Confirmed,
  Refused
};

inline constexpr std::size_t CONTROL_STATE_COUNT = 8;

/// The state's name as sprite names spell it: "Idle", "Hover", ... "Refused".
[[nodiscard]] constexpr std::string_view ControlStateName(ControlState _state) noexcept
{
  switch (_state)
  {
  case ControlState::Idle:
    return "Idle";
  case ControlState::Hover:
    return "Hover";
  case ControlState::Pressed:
    return "Pressed";
  case ControlState::Selected:
    return "Selected";
  case ControlState::Disabled:
    return "Disabled";
  case ControlState::Pending:
    return "Pending";
  case ControlState::Confirmed:
    return "Confirmed";
  case ControlState::Refused:
    return "Refused";
  }
  return "Idle";
}

} // namespace Neuron
