#include "FlightInput.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Outpost
{

void FlightInput::Bind(const Neuron::CommandMap& _commands)
{
  m_ids.throttleUp = _commands.FindCommand("ThrottleUp");
  m_ids.throttleDown = _commands.FindCommand("ThrottleDown");
  m_ids.throttleStepUp = _commands.FindCommand("ThrottleStepUp");
  m_ids.throttleStepDown = _commands.FindCommand("ThrottleStepDown");
  m_ids.reverse = _commands.FindCommand("Reverse");
  m_ids.engineKill = _commands.FindCommand("EngineKill");
  m_ids.cruise = _commands.FindCommand("Cruise");
  m_ids.thruster = _commands.FindCommand("Thruster");
  m_ids.strafeLeft = _commands.FindCommand("StrafeLeft");
  m_ids.strafeRight = _commands.FindCommand("StrafeRight");
  m_ids.strafeUp = _commands.FindCommand("StrafeUp");
  m_ids.strafeDown = _commands.FindCommand("StrafeDown");
  m_ids.rollLeft = _commands.FindCommand("RollLeft");
  m_ids.rollRight = _commands.FindCommand("RollRight");
  m_ids.noseUp = _commands.FindCommand("NoseUp");
  m_ids.noseDown = _commands.FindCommand("NoseDown");
  m_ids.yawLeft = _commands.FindCommand("YawLeft");
  m_ids.yawRight = _commands.FindCommand("YawRight");
  m_ids.mouseFlight = _commands.FindCommand("MouseFlight");
  m_ids.mouseFlightLock = _commands.FindCommand("MouseFlightLock");
  m_ids.cancel = _commands.FindCommand("Cancel");
}

void FlightInput::SetThrottle(float _throttle) noexcept
{
  m_throttle = std::clamp(_throttle, 0.0f, 1.0f);
  if (m_throttle <= 0.0f)
  {
    m_cruise = false;
  }
}

void FlightInput::OnCommand(const Neuron::CommandEvent& _event)
{
  if (_event.phase == Neuron::CommandPhase::Released)
  {
    if (_event.command == m_ids.mouseFlight)
    {
      m_pressed = false;
      m_armed = false;
    }
    return;
  }
  const Neuron::CommandId command = _event.command;
  if (command == m_ids.throttleStepUp || command == m_ids.throttleStepDown)
  {
    m_order = Manoeuvre::FreeFlight();
    if (command == m_ids.throttleStepDown)
    {
      m_cruise = false;
    }
    SetThrottle(m_throttle + (command == m_ids.throttleStepUp ? THROTTLE_STEP : -THROTTLE_STEP));
  }
  else if (command == m_ids.engineKill)
  {
    m_engineOn = !m_engineOn;
  }
  else if (command == m_ids.cruise)
  {
    m_cruise = !m_cruise;
    if (m_cruise)
    {
      m_throttle = 1.0f;
      m_engineOn = true;
    }
  }
  else if (command == m_ids.mouseFlightLock)
  {
    m_locked = !m_locked;
  }
  else if (command == m_ids.mouseFlight && _event.phase == Neuron::CommandPhase::Pressed)
  {
    m_pressed = true;
    m_armed = false;
    m_pressTicks = 0;
    m_pressX = m_cursorX;
    m_pressY = m_cursorY;
  }
  else if (command == m_ids.cancel)
  {
    Cancel();
  }
}

void FlightInput::SetCursor(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels) noexcept
{
  m_cursorX = _xPixels;
  m_cursorY = _yPixels;
  m_width = std::max(1.0f, _widthPixels);
  m_height = std::max(1.0f, _heightPixels);
}

void FlightInput::GoTo(const WorldPosition& _target, float _standOffMetres) noexcept
{
  m_order = Manoeuvre::GoTo(_target, _standOffMetres);
  m_locked = false;
}

void FlightInput::Cancel() noexcept
{
  m_order = Manoeuvre::FreeFlight();
  m_locked = false;
}

void FlightInput::RefuseCruise() noexcept
{
  m_cruise = false;
}

void FlightInput::SetMouseEnabled(bool _enabled) noexcept
{
  m_mouseEnabled = _enabled;
  if (!_enabled)
  {
    m_pressed = false;
    m_armed = false;
  }
}

XMFLOAT2 FlightInput::CursorDemand(float _x, float _y, float _width, float _height) noexcept
{
  const float halfWidth = std::max(0.5f, _width * 0.5f);
  const float halfHeight = std::max(0.5f, _height * 0.5f);
  return XMFLOAT2{std::clamp((_x - halfWidth) / halfWidth, -1.0f, 1.0f), std::clamp((_y - halfHeight) / halfHeight, -1.0f, 1.0f)};
}

float FlightInput::BankTarget(float _maxBankDegrees, float _yawDemand, float _throttle) noexcept
{
  return -_maxBankDegrees * std::clamp(_yawDemand, -1.0f, 1.0f) * std::max(BANK_THROTTLE_FLOOR, std::min(1.0f, std::abs(_throttle)));
}

float FlightInput::BankOf(const XMFLOAT4& _orientation, const XMFLOAT3& _up) noexcept
{
  const XMVECTOR right = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMLoadFloat4(&_orientation));
  const float lift = std::clamp(XMVectorGetX(XMVector3Dot(right, XMVector3Normalize(XMLoadFloat3(&_up)))), -1.0f, 1.0f);
  return XMConvertToDegrees(std::asin(lift));
}

XMFLOAT2 FlightInput::Demand() const noexcept
{
  if (!MouseSteering())
  {
    return XMFLOAT2{0.0f, 0.0f};
  }
  const XMFLOAT2 demand = CursorDemand(m_cursorX, m_cursorY, m_width, m_height);
  return XMFLOAT2{std::clamp(demand.x * TURN_SENSITIVITY, -1.0f, 1.0f), std::clamp(demand.y * TURN_SENSITIVITY, -1.0f, 1.0f)};
}

ShipCommand FlightInput::Sample(const Neuron::CommandMap& _commands, const FlightState& _predicted, const XMFLOAT3& _referenceUp,
                                float _maxBankDegrees, std::uint32_t _ticksPerSecond)
{
  const float tickSeconds = 1.0f / static_cast<float>(std::max(1u, _ticksPerSecond));
  const auto held = [&_commands](Neuron::CommandId _command) { return _commands.IsHeld(_command); };

  // The hold threshold arms the mouse.
  if (m_pressed && !m_armed)
  {
    ++m_pressTicks;
    const float dx = m_cursorX - m_pressX;
    const float dy = m_cursorY - m_pressY;
    const float radius = m_width * CLICK_RADIUS_SHARE_OF_WIDTH;
    m_armed = static_cast<float>(m_pressTicks) * tickSeconds >= HOLD_SECONDS - 1.0e-4f || dx * dx + dy * dy > radius * radius;
  }

  // Throttle keys move the throttle; throttling down drops cruise.
  const bool up = held(m_ids.throttleUp);
  const bool down = held(m_ids.throttleDown);
  if (up != down)
  {
    m_order = Manoeuvre::FreeFlight();
    if (down)
    {
      m_cruise = false;
    }
    SetThrottle(m_throttle + (up ? THROTTLE_PER_SECOND : -THROTTLE_PER_SECOND) * tickSeconds);
  }

  ShipCommand command;
  FlightControls& wishes = command.wishes;
  wishes.throttle = m_throttle;
  wishes.reverse = held(m_ids.reverse);
  wishes.thruster = held(m_ids.thruster);
  wishes.engineOn = m_engineOn;
  wishes.cruise = m_cruise;
  if (held(m_ids.strafeLeft))
  {
    wishes.strafe = StrafeDirection::Left;
  }
  else if (held(m_ids.strafeRight))
  {
    wishes.strafe = StrafeDirection::Right;
  }
  else if (held(m_ids.strafeUp))
  {
    wishes.strafe = StrafeDirection::Up;
  }
  else if (held(m_ids.strafeDown))
  {
    wishes.strafe = StrafeDirection::Down;
  }

  // Steering: the mouse, or the keys when the mouse is not steering. Positive pitch turns the nose
  // down and positive yaw turns it right, as the cursor's y and x grow.
  XMFLOAT2 demand = Demand();
  if (!MouseSteering())
  {
    demand.x = (held(m_ids.yawRight) ? 1.0f : 0.0f) - (held(m_ids.yawLeft) ? 1.0f : 0.0f);
    demand.y = (held(m_ids.noseDown) ? 1.0f : 0.0f) - (held(m_ids.noseUp) ? 1.0f : 0.0f);
  }
  const float roll = (held(m_ids.rollLeft) ? 1.0f : 0.0f) - (held(m_ids.rollRight) ? 1.0f : 0.0f);
  if (demand.x != 0.0f || demand.y != 0.0f || roll != 0.0f)
  {
    m_order = Manoeuvre::FreeFlight();
  }
  wishes.steering.x = demand.y;
  wishes.steering.y = demand.x;
  if (roll != 0.0f)
  {
    wishes.steering.z = roll;
  }
  else
  {
    // Bank into the turn, and level against the camera's up out of it.
    const float target = BankTarget(_maxBankDegrees, demand.x, m_throttle);
    wishes.steering.z = std::clamp((target - BankOf(_predicted.orientation, _referenceUp)) / BANK_GAIN_DEGREES, -1.0f, 1.0f);
  }
  command.manoeuvre = m_order;
  return command;
}

} // namespace Outpost
