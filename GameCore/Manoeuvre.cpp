#include "Manoeuvre.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Outpost
{

namespace
{

// How hard the autopilot turns towards its heading: the commanded turn rate is this many radians
// a second per radian of error. Against the light fighter's 0.19 s response it settles without
// overshoot.
constexpr float TURN_GAIN_PER_SECOND = 2.0f;
// The commanded closing speed is this share of distance times the drag rate D/m. At 0.5 the
// approach is damped at about 0.7 of critical, so a ship overshoots its stand-off by a few percent.
constexpr float APPROACH_GAIN = 0.5f;

/// Steering towards a world direction: the axis throttle that turns the nose onto it, and the
/// cosine of the angle still to turn.
[[nodiscard]] XMFLOAT3 SteerTowards(FXMVECTOR _direction, const ShipFlightSpec& _spec, const FlightState& _state,
                                    float& _outCosine) noexcept
{
  const XMVECTOR local = XMVector3Rotate(_direction, XMQuaternionConjugate(XMLoadFloat4(&_state.orientation)));
  const XMVECTOR nose = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
  XMVECTOR axis = XMVector3Cross(nose, local);
  const float sine = XMVectorGetX(XMVector3Length(axis));
  _outCosine = XMVectorGetX(XMVector3Dot(nose, local));
  const float angle = std::atan2(sine, _outCosine);
  if (sine < 1.0e-6f)
  {
    if (_outCosine > 0.0f)
    {
      return XMFLOAT3{}; // already on it
    }
    axis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // directly behind: yaw round
  }
  const XMVECTOR rate = XMVectorScale(XMVector3Normalize(axis), angle * TURN_GAIN_PER_SECOND);
  XMFLOAT3 desired{};
  XMStoreFloat3(&desired, rate);
  // Each axis's steady turn rate is torque over angular drag times its axis throttle.
  const auto throttleFor = [](float _rate, float _torque, float _drag)
  { return _torque > 0.0f ? std::clamp(_rate * _drag / _torque, -1.0f, 1.0f) : 0.0f; };
  return XMFLOAT3{throttleFor(desired.x, _spec.steeringTorqueNewtonMetres.x, _spec.angularDragNewtonMetreSeconds.x),
                  throttleFor(desired.y, _spec.steeringTorqueNewtonMetres.y, _spec.angularDragNewtonMetreSeconds.y), 0.0f};
}

[[nodiscard]] ManoeuvreOutput FlyTo(const UniversePosition& _target, float _standOffMetres, const ShipFlightSpec& _spec,
                                    const FlightState& _state) noexcept
{
  ManoeuvreOutput output;
  // The offset is taken in double and only then narrowed, so a target far from the origin is as
  // precise as one near it (ADR-007).
  const XMVECTOR offset = XMVectorSet(static_cast<float>(_target.x - _state.position.x), static_cast<float>(_target.y - _state.position.y),
                                      static_cast<float>(_target.z - _state.position.z), 0.0f);
  const float distance = XMVectorGetX(XMVector3Length(offset));
  const float remaining = distance - _standOffMetres;
  if (remaining <= ARRIVAL_TOLERANCE_METRES && _state.SpeedMetresPerSecond() <= ARRIVAL_SPEED_METRES_PER_SECOND)
  {
    output.arrived = true;
    return output; // throttle 0 and no steering: drag holds the ship
  }
  if (remaining <= 0.0f || distance < 1.0e-3f)
  {
    return output; // inside the stand-off: let drag stop the ship
  }
  float cosine = 1.0f;
  output.controls.steering = SteerTowards(XMVectorScale(offset, 1.0f / distance), _spec, _state, cosine);
  // Close at a speed the ship can still shed by the stand-off, and only while roughly facing it.
  const float dragRate = (_spec.hullDragKilogramsPerSecond + _spec.engineDragKilogramsPerSecond) / _spec.massKilograms;
  const float topSpeed = _spec.TopSpeedMetresPerSecond();
  const float closingSpeed = std::min(topSpeed, APPROACH_GAIN * dragRate * remaining);
  const float facing = std::max(0.0f, cosine);
  output.controls.throttle = topSpeed > 0.0f ? closingSpeed / topSpeed * facing * facing : 0.0f;
  return output;
}

} // namespace

FlightControls Arbitrate(const FlightControls& _wishes, bool _wayAheadObstructed) noexcept
{
  FlightControls controls = _wishes;
  if (_wayAheadObstructed)
  {
    controls.cruise = false;
  }
  if (controls.reverse)
  {
    controls.throttle = -1.0f;
    controls.cruise = false;
  }
  if (!controls.engineOn)
  {
    controls.cruise = false;
  }
  return controls;
}

ManoeuvreOutput RunManoeuvre(const Manoeuvre& _manoeuvre, const FlightControls& _input, const ShipFlightSpec& _spec,
                             const FlightState& _state, bool _wayAheadObstructed) noexcept
{
  ManoeuvreOutput output;
  switch (_manoeuvre.kind)
  {
  case ManoeuvreKind::FreeFlight:
    output.controls = _input;
    break;
  case ManoeuvreKind::HoldPosition:
  {
    const double dx = _manoeuvre.target.x - _state.position.x;
    const double dy = _manoeuvre.target.y - _state.position.y;
    const double dz = _manoeuvre.target.z - _state.position.z;
    if (std::sqrt(dx * dx + dy * dy + dz * dz) <= static_cast<double>(HOLD_TOLERANCE_METRES))
    {
      output.arrived = true; // throttle 0: drag settles the ship
    }
    else
    {
      output = FlyTo(_manoeuvre.target, 0.0f, _spec, _state);
      output.arrived = false;
    }
    break;
  }
  case ManoeuvreKind::GoTo:
    output = FlyTo(_manoeuvre.target, _manoeuvre.standOffMetres, _spec, _state);
    break;
  }
  output.controls = Arbitrate(output.controls, _wayAheadObstructed);
  return output;
}

} // namespace Outpost
