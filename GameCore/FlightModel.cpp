#include "FlightModel.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Outpost
{

namespace
{

/// The share of a tick's motion that a velocity decaying at rate _k keeps: (1 - e^(-k dt)) / k,
/// in seconds, which tends to dt as k tends to 0.
[[nodiscard]] float DecayIntegral(float _k, float _dt) noexcept
{
  const float kdt = _k * _dt;
  return kdt < 1.0e-6f ? _dt : -std::expm1(-kdt) / _k;
}

/// One axis of the relaxation: the velocity after the tick, and the displacement during it.
void Relax(float _velocity, float _force, float _drag, float _mass, float _dt, float& _outVelocity, float& _outDisplacement) noexcept
{
  const float k = _drag / _mass;
  if (_drag <= 0.0f)
  {
    // No drag at all: constant acceleration.
    const float acceleration = _force / _mass;
    _outVelocity = _velocity + acceleration * _dt;
    _outDisplacement = (_velocity + 0.5f * acceleration * _dt) * _dt;
    return;
  }
  const float terminal = _force / _drag;
  const float keep = std::exp(-k * _dt);
  _outVelocity = terminal + (_velocity - terminal) * keep;
  _outDisplacement = terminal * _dt + (_velocity - terminal) * DecayIntegral(k, _dt);
}

[[nodiscard]] std::int32_t SecondsToTicks(float _seconds, std::uint32_t _ticksPerSecond) noexcept
{
  return static_cast<std::int32_t>(std::lround(_seconds * static_cast<float>(_ticksPerSecond)));
}

[[nodiscard]] bool CruiseEngaged(CruiseState _cruise) noexcept
{
  return _cruise == CruiseState::Charging || _cruise == CruiseState::Cruising;
}

/// Pays _cost from _pool if it can, and reports whether it did.
[[nodiscard]] bool Pay(float& _pool, float _cost) noexcept
{
  if (_pool < _cost)
  {
    return false;
  }
  _pool -= _cost;
  return true;
}

} // namespace

float ShipFlightSpec::TopSpeedMetresPerSecond() const noexcept
{
  const float drag = hullDragKilogramsPerSecond + engineDragKilogramsPerSecond;
  return drag > 0.0f ? engineThrustNewtons / drag : 0.0f;
}

ShipFlightSpec FlightSpecFor(const ShipContent& _content, const ShipArchetype& _ship)
{
  ShipFlightSpec spec;
  spec.massKilograms = _ship.massKilograms;
  spec.inertiaKilogramSquareMetres = _ship.inertiaKilogramSquareMetres;
  spec.hullDragKilogramsPerSecond = _ship.hullDragKilogramsPerSecond;
  spec.steeringTorqueNewtonMetres = _ship.steeringTorqueNewtonMetres;
  spec.angularDragNewtonMetreSeconds = _ship.angularDragNewtonMetreSeconds;
  spec.strafeForceNewtons = _ship.strafeForceNewtons;
  spec.strafeDrainPerSecond = _ship.strafeDrainPerSecond;
  if (const EngineArchetype* engine = _content.FindEngine(_ship.engine))
  {
    spec.engineThrustNewtons = engine->thrustNewtons;
    spec.engineDragKilogramsPerSecond = engine->dragKilogramsPerSecond;
    spec.reverseFraction = engine->reverseFraction;
    spec.cruiseChargeSeconds = engine->cruiseChargeSeconds;
    spec.cruisePowerPerSecond = engine->cruisePowerPerSecond;
  }
  if (const PowerPlantArchetype* plant = _content.FindPowerPlant(_ship.powerPlant))
  {
    spec.powerCapacity = plant->powerCapacity;
    spec.powerChargePerSecond = plant->powerChargePerSecond;
    spec.thrustCapacity = plant->thrustCapacity;
    spec.thrustChargePerSecond = plant->thrustChargePerSecond;
  }
  if (const ThrusterArchetype* thruster = _content.FindThruster(_ship.thruster))
  {
    spec.thrusterForceNewtons = thruster->forceNewtons;
    spec.thrusterDrainPerSecond = thruster->drainPerSecond;
  }
  return spec;
}

float FlightState::SpeedMetresPerSecond() const noexcept
{
  return XMVectorGetX(XMVector3Length(XMLoadFloat3(&velocityMetresPerSecond)));
}

FlightState RestingFlightState(const ShipFlightSpec& _spec, const Pose& _pose) noexcept
{
  FlightState state;
  state.position = _pose.position;
  state.orientation = _pose.orientation;
  state.powerPool = _spec.powerCapacity;
  state.thrustPool = _spec.thrustCapacity;
  return state;
}

XMVECTOR NoseDirection(const FlightState& _state) noexcept
{
  return XMVector3Rotate(XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), XMLoadFloat4(&_state.orientation));
}

void DisruptCruise(std::uint32_t _ticksPerSecond, FlightState& _state) noexcept
{
  if (CruiseEngaged(_state.cruise))
  {
    _state.cruise = CruiseState::Cooldown;
    _state.cruiseTimerTicks = -SecondsToTicks(CRUISE_DISRUPTION_COOLDOWN_SECONDS, _ticksPerSecond);
  }
}

void StepFlight(const ShipFlightSpec& _spec, const FlightControls& _controls, std::uint32_t _ticksPerSecond, FlightState& _state)
{
  const float dt = 1.0f / static_cast<float>(_ticksPerSecond);
  const float throttle = std::clamp(_controls.throttle, -1.0f, 1.0f);

  // 1. The power plant fills both pools.
  _state.powerPool = std::min(_spec.powerCapacity, _state.powerPool + _spec.powerChargePerSecond * dt);
  _state.thrustPool = std::min(_spec.thrustCapacity, _state.thrustPool + _spec.thrustChargePerSecond * dt);

  // 2. Cruise (ch. 02 §3.8). A killed engine, a zero throttle or a dropped wish disengages.
  const bool cruiseAllowed = _controls.engineOn && throttle > 0.0f;
  switch (_state.cruise)
  {
  case CruiseState::Off:
    if (_controls.cruise && cruiseAllowed)
    {
      _state.cruise = CruiseState::Charging;
      _state.cruiseTimerTicks = 0;
    }
    break;
  case CruiseState::Cooldown:
    if (_controls.cruise && cruiseAllowed)
    {
      _state.cruise = CruiseState::Charging; // the timer runs on from its negative value
    }
    else if (++_state.cruiseTimerTicks >= 0)
    {
      _state.cruise = CruiseState::Off;
      _state.cruiseTimerTicks = 0;
    }
    break;
  case CruiseState::Charging:
  case CruiseState::Cruising:
    if (!_controls.cruise || !cruiseAllowed)
    {
      _state.cruise = CruiseState::Off;
      _state.cruiseTimerTicks = 0;
    }
    break;
  }
  const float drag = _spec.hullDragKilogramsPerSecond +
                     (_controls.engineOn ? _spec.engineDragKilogramsPerSecond +
                                             (CruiseEngaged(_state.cruise) ? CRUISE_EXTRA_DRAG_KILOGRAMS_PER_SECOND : 0.0f)
                                         : 0.0f);
  if (_state.cruise == CruiseState::Charging && ++_state.cruiseTimerTicks >= SecondsToTicks(_spec.cruiseChargeSeconds, _ticksPerSecond))
  {
    _state.cruise = CruiseState::Cruising;
    // The ramp starts where normal flight tops out, so the hand-over is seamless.
    _state.cruiseRamp = std::min(1.0f, _spec.engineThrustNewtons / drag / CRUISE_SPEED_METRES_PER_SECOND);
  }
  else if (_state.cruise == CruiseState::Cruising)
  {
    _state.cruiseRamp = std::min(1.0f, _state.cruiseRamp + dt / CRUISE_RAMP_SECONDS);
  }

  // 3. Forces in the ship's frame, each paid for from its pool.
  float forwardForce = 0.0f; // along the nose
  if (_controls.engineOn)
  {
    if (CruiseEngaged(_state.cruise))
    {
      // No thrust at all in a tick the power pool cannot pay for.
      if (Pay(_state.powerPool, _spec.cruisePowerPerSecond * dt))
      {
        forwardForce = _state.cruise == CruiseState::Cruising ? drag * CRUISE_SPEED_METRES_PER_SECOND * throttle * _state.cruiseRamp
                                                              : throttle * _spec.engineThrustNewtons;
      }
    }
    else
    {
      forwardForce = throttle * _spec.engineThrustNewtons * (throttle < 0.0f ? _spec.reverseFraction : 1.0f);
    }
  }
  // The thruster is refused in cruise and switches off when its pool cannot pay.
  _state.thrusterActive = _controls.thruster && !CruiseEngaged(_state.cruise) && Pay(_state.thrustPool, _spec.thrusterDrainPerSecond * dt);
  if (_state.thrusterActive)
  {
    forwardForce += _spec.thrusterForceNewtons;
  }
  XMVECTOR localForce = XMVectorSet(0.0f, 0.0f, -forwardForce, 0.0f);
  if (_controls.strafe != StrafeDirection::None && Pay(_state.thrustPool, _spec.strafeDrainPerSecond * dt))
  {
    const float side = _spec.strafeForceNewtons;
    switch (_controls.strafe)
    {
    case StrafeDirection::Left:
      localForce = XMVectorAdd(localForce, XMVectorSet(-side, 0.0f, 0.0f, 0.0f));
      break;
    case StrafeDirection::Right:
      localForce = XMVectorAdd(localForce, XMVectorSet(side, 0.0f, 0.0f, 0.0f));
      break;
    case StrafeDirection::Up:
      localForce = XMVectorAdd(localForce, XMVectorSet(0.0f, side, 0.0f, 0.0f));
      break;
    case StrafeDirection::Down:
      localForce = XMVectorAdd(localForce, XMVectorSet(0.0f, -side, 0.0f, 0.0f));
      break;
    case StrafeDirection::None:
      break;
    }
  }
  const XMVECTOR orientation = XMLoadFloat4(&_state.orientation);
  XMFLOAT3 force{};
  XMStoreFloat3(&force, XMVector3Rotate(localForce, orientation));

  // 4. Linear relaxation, per world axis: the drag is isotropic, so the axes are independent.
  XMFLOAT3& velocity = _state.velocityMetresPerSecond;
  XMFLOAT3 displacement{};
  Relax(velocity.x, force.x, drag, _spec.massKilograms, dt, velocity.x, displacement.x);
  Relax(velocity.y, force.y, drag, _spec.massKilograms, dt, velocity.y, displacement.y);
  Relax(velocity.z, force.z, drag, _spec.massKilograms, dt, velocity.z, displacement.z);
  const float speed = XMVectorGetX(XMVector3Length(XMLoadFloat3(&velocity)));
  if (speed > MAX_SPEED_METRES_PER_SECOND)
  {
    XMStoreFloat3(&velocity, XMVectorScale(XMLoadFloat3(&velocity), MAX_SPEED_METRES_PER_SECOND / speed));
  }

  // 5. Angular relaxation, per ship axis: torque is steering torque times the axis throttle.
  XMFLOAT3& spin = _state.angularVelocityRadiansPerSecond;
  XMFLOAT3 turn{};
  const auto axisThrottle = [](float _value) { return std::clamp(_value, -1.0f, 1.0f); };
  Relax(spin.x, _spec.steeringTorqueNewtonMetres.x * axisThrottle(_controls.steering.x), _spec.angularDragNewtonMetreSeconds.x,
        _spec.inertiaKilogramSquareMetres.x, dt, spin.x, turn.x);
  Relax(spin.y, _spec.steeringTorqueNewtonMetres.y * axisThrottle(_controls.steering.y), _spec.angularDragNewtonMetreSeconds.y,
        _spec.inertiaKilogramSquareMetres.y, dt, spin.y, turn.y);
  Relax(spin.z, _spec.steeringTorqueNewtonMetres.z * axisThrottle(_controls.steering.z), _spec.angularDragNewtonMetreSeconds.z,
        _spec.inertiaKilogramSquareMetres.z, dt, spin.z, turn.z);

  // 6. The pose advances: position in double, rotation in the ship's own frame, limited per tick.
  _state.position.x += static_cast<double>(displacement.x);
  _state.position.y += static_cast<double>(displacement.y);
  _state.position.z += static_cast<double>(displacement.z);
  const XMVECTOR turnVector = XMLoadFloat3(&turn);
  const float angle = std::min(XMVectorGetX(XMVector3Length(turnVector)), MAX_ROTATION_RADIANS_PER_TICK);
  if (angle > 0.0f)
  {
    const XMVECTOR delta = XMQuaternionRotationNormal(XMVector3Normalize(turnVector), angle);
    XMStoreFloat4(&_state.orientation, XMQuaternionNormalize(XMQuaternionMultiply(delta, orientation)));
  }
}

} // namespace Outpost
