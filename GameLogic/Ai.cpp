#include "Ai.h"

#include "Manoeuvre.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Outpost
{

namespace
{

[[nodiscard]] XMVECTOR Towards(const UniversePosition& _from, const UniversePosition& _to) noexcept
{
  return XMVectorSet(static_cast<float>(_to.x - _from.x), static_cast<float>(_to.y - _from.y), static_cast<float>(_to.z - _from.z), 0.0f);
}

[[nodiscard]] UniversePosition Along(const UniversePosition& _from, FXMVECTOR _offset) noexcept
{
  XMFLOAT3 offset{};
  XMStoreFloat3(&offset, _offset);
  return UniversePosition{_from.x + static_cast<double>(offset.x), _from.y + static_cast<double>(offset.y),
                          _from.z + static_cast<double>(offset.z)};
}

} // namespace

Ai::Ai(const PilotRecord& _pilot, std::uint64_t _seed)
  : m_pilot(_pilot),
    m_random(_seed)
{
}

std::int32_t Ai::Ticks(float _seconds, std::uint32_t _ticksPerSecond) const noexcept
{
  return std::max(1, static_cast<std::int32_t>(std::lround(_seconds * static_cast<float>(_ticksPerSecond))));
}

std::int32_t Ai::Jittered(float _seconds, std::uint32_t _ticksPerSecond) noexcept
{
  // Up to half either way (ch. 05 §3.8).
  return Ticks(_seconds * m_random.Range(0.5f, 1.5f), _ticksPerSecond);
}

void Ai::Enter(AiState _state, std::int32_t _ticks) noexcept
{
  m_state = _state;
  m_stateTicks = _ticks;
}

UniversePosition Ai::AimPoint(const AiView& _view, const AiContact& _target, float _distance) noexcept
{
  const FlightState& own = *_view.own;
  XMFLOAT3 offset{};
  XMStoreFloat3(&offset, Towards(own.position, _target.position));
  XMFLOAT3 relative{};
  XMStoreFloat3(&relative, XMVectorSubtract(XMLoadFloat3(&_target.velocityMetresPerSecond), XMLoadFloat3(&own.velocityMetresPerSecond)));
  const float lead = LeadSeconds(offset, relative, _view.muzzleSpeedMetresPerSecond).value_or(0.0f);
  const XMVECTOR leadOffset = XMVectorAdd(XMLoadFloat3(&offset), XMVectorScale(XMLoadFloat3(&relative), lead));
  // The error is held in the target's frame and drawn again every so often (§3.8): an angle of
  // cone * u^exponent, in a random direction across the line of sight.
  if (--m_rerollTicks <= 0)
  {
    m_rerollTicks = Ticks(m_random.Range(m_pilot.rerollMinSeconds, m_pilot.rerollMaxSeconds), _view.ticksPerSecond);
    const float size = XMConvertToRadians(m_pilot.aimConeDegrees) * std::pow(m_random.NextFloat(), m_pilot.aimExponent);
    m_errorAngles = XMFLOAT2{size, m_random.Range(0.0f, XM_2PI)};
  }
  const float speed = XMVectorGetX(XMVector3Length(XMLoadFloat3(&_target.velocityMetresPerSecond)));
  if (speed < STILL_TARGET_METRES_PER_SECOND)
  {
    return Along(own.position, leadOffset);
  }
  const XMVECTOR forward = XMVector3Normalize(leadOffset);
  XMVECTOR across = XMVector3Cross(forward, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
  if (XMVectorGetX(XMVector3LengthSq(across)) < 1.0e-6f)
  {
    across = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
  }
  across = XMVector3Normalize(across);
  const XMVECTOR up = XMVector3Cross(across, forward);
  const XMVECTOR direction = XMVectorAdd(XMVectorScale(across, std::cos(m_errorAngles.y)), XMVectorScale(up, std::sin(m_errorAngles.y)));
  const float miss = _distance * std::tan(m_errorAngles.x);
  return Along(own.position, XMVectorAdd(leadOffset, XMVectorScale(direction, miss)));
}

ShipCommand Ai::Think(const AiView& _view)
{
  ShipCommand command;
  command.manoeuvre = Manoeuvre::FreeFlight();
  const FlightState& own = *_view.own;
  const ShipFlightSpec& spec = *_view.spec;
  if (m_state == AiState::Gone)
  {
    return command;
  }

  // The target: the nearest enemy ship it can attack within detection range. A pilot that is
  // fleeing keeps running from the one it had, and is gone once far from every enemy.
  const AiContact* target = nullptr;
  float distance = 0.0f;
  float nearestPlayer = INFINITY;
  for (const AiContact& contact : _view.contacts)
  {
    if (contact.object == _view.self || contact.owner != _view.enemy)
    {
      continue;
    }
    const float d = XMVectorGetX(XMVector3Length(Towards(own.position, contact.position)));
    nearestPlayer = std::min(nearestPlayer, d);
    const bool keep = m_state == AiState::Flee ? contact.object == m_target : contact.attackable && d <= m_pilot.detectionMetres;
    if (keep && (target == nullptr || d < distance))
    {
      target = &contact;
      distance = d;
    }
  }

  if (m_state != AiState::Flee && m_state != AiState::Idle &&
      (_view.corePercent < m_pilot.fleeCorePercent || _view.hullPercent < m_pilot.fleeHullPercent))
  {
    Enter(AiState::Flee, 0);
  }
  if (m_state == AiState::Flee)
  {
    if (nearestPlayer > m_pilot.goneMetres)
    {
      Enter(AiState::Gone, 0);
      return command;
    }
    // Away from the target, or straight on when there is none, at full throttle and with the thruster.
    const XMVECTOR away = target != nullptr ? XMVectorNegate(Towards(own.position, target->position)) : NoseDirection(own);
    XMFLOAT3 direction{};
    XMStoreFloat3(&direction, XMVector3Normalize(away));
    float cosine = 1.0f;
    command.wishes.steering = SteeringTowards(direction, spec, own, cosine);
    command.wishes.throttle = 1.0f;
    command.wishes.thruster = cosine > 0.9f && own.thrustPool > 0.0f;
    return command;
  }

  if (target == nullptr)
  {
    if (!m_anchored)
    {
      m_anchor = own.position;
      m_anchored = true;
    }
    m_target = NO_OBJECT;
    m_state = AiState::Idle;
    command.manoeuvre = Manoeuvre::Hold(m_anchor);
    return command;
  }
  m_anchored = false;
  if (m_target != target->object || m_state == AiState::Idle)
  {
    m_target = target->object;
    Enter(AiState::RunIn, 0);
  }

  const XMVECTOR toTarget = XMVector3Normalize(Towards(own.position, target->position));
  const XMVECTOR nose = NoseDirection(own);
  const XMVECTOR targetNose = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), XMLoadFloat4(&target->orientation));
  const float targetSpeed = XMVectorGetX(XMVector3Length(XMLoadFloat3(&target->velocityMetresPerSecond)));
  const bool targetAhead = XMVectorGetX(XMVector3Dot(nose, toTarget)) > 0.0f;
  const bool behindTarget = XMVectorGetX(XMVector3Dot(targetNose, toTarget)) > 0.0f; // the target faces away
  const UniversePosition aim = AimPoint(_view, *target, distance);
  command.aim = aim;

  // Choosing the behaviour (ch. 05 §3.3 and §3.4, simplified to one target).
  if (m_state != AiState::Evade && distance < m_pilot.evadeMetres && !targetAhead && !behindTarget)
  {
    Enter(AiState::Evade, Ticks(m_pilot.dodgeSeconds * 2.0f, _view.ticksPerSecond));
    m_dodge = XMFLOAT3{};
  }
  else if (m_state != AiState::Evade && m_state != AiState::Pass && behindTarget && targetSpeed >= TRAIL_SPEED_METRES_PER_SECOND &&
           distance < m_pilot.turnInMetres)
  {
    m_state = AiState::Trail;
  }
  else if (m_state == AiState::Trail && (!behindTarget || distance >= m_pilot.turnInMetres))
  {
    Enter(AiState::RunIn, 0);
  }

  XMVECTOR heading = Towards(own.position, aim);
  float throttle = 1.0f;
  switch (m_state)
  {
  case AiState::RunIn:
    if (distance < m_pilot.passMetres)
    {
      Enter(AiState::Pass, Ticks(m_pilot.passSeconds, _view.ticksPerSecond));
      heading = nose;
    }
    break;
  case AiState::Pass:
    heading = nose;
    if (--m_stateTicks <= 0)
    {
      Enter(AiState::Extend, Ticks(m_pilot.extendSeconds, _view.ticksPerSecond));
    }
    break;
  case AiState::Extend:
  {
    // Away from the target, sliding off to one side so the next pass comes from another angle.
    const XMVECTOR side = XMVector3Normalize(XMVector3Cross(toTarget, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
    heading = XMVectorAdd(XMVectorNegate(toTarget), XMVectorScale(side, 0.4f));
    if (--m_stateTicks <= 0 || distance > m_pilot.turnInMetres)
    {
      Enter(AiState::RunIn, 0);
    }
    break;
  }
  case AiState::Trail:
  {
    // Hold the trail distance: close faster the further out, match the target's speed when there.
    const float topSpeed = std::max(1.0f, spec.TopSpeedMetresPerSecond());
    throttle = std::clamp((distance - m_pilot.trailMetres) / m_pilot.trailMetres + targetSpeed / topSpeed, 0.0f, 1.0f);
    break;
  }
  case AiState::Evade:
  {
    if (m_stateTicks % Ticks(m_pilot.dodgeSeconds, _view.ticksPerSecond) == 0 ||
        XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&m_dodge))) == 0.0f)
    {
      // A waggle: a random direction within 60 degrees of the nose.
      const XMVECTOR axis = XMVector3Normalize(
        XMVectorSet(m_random.Range(-1.0f, 1.0f), m_random.Range(-1.0f, 1.0f), m_random.Range(-1.0f, 1.0f) + 1.0e-3f, 0.0f));
      XMStoreFloat3(&m_dodge, XMVector3Rotate(nose, XMQuaternionRotationAxis(axis, XMConvertToRadians(m_random.Range(20.0f, 60.0f)))));
    }
    heading = XMLoadFloat3(&m_dodge);
    command.wishes.thruster = distance < 50.0f && own.thrustPool > 0.0f;
    if (--m_stateTicks <= 0)
    {
      Enter(AiState::Extend, Ticks(m_pilot.extendSeconds * 0.5f, _view.ticksPerSecond));
    }
    break;
  }
  default:
    break;
  }

  XMFLOAT3 direction{};
  XMStoreFloat3(&direction, XMVector3Normalize(heading));
  float cosine = 1.0f;
  command.wishes.steering = SteeringTowards(direction, spec, own, cosine);
  command.wishes.throttle = throttle;

  // Fire control (§3.8): bursts and pauses, within range, with the nose on the aim point.
  // A positive count is a burst running down, a negative one a pause running up; each ends in the other.
  if (m_burstTicks > 0)
  {
    --m_burstTicks;
    if (m_burstTicks == 0)
    {
      m_burstTicks = -Jittered(std::max(m_pilot.pauseSeconds, 0.05f), _view.ticksPerSecond);
    }
  }
  else if (m_burstTicks < 0)
  {
    ++m_burstTicks;
    if (m_burstTicks == 0)
    {
      m_burstTicks = Jittered(m_pilot.burstSeconds, _view.ticksPerSecond);
    }
  }
  else
  {
    m_burstTicks = Jittered(m_pilot.burstSeconds, _view.ticksPerSecond);
  }
  const XMVECTOR toAim = XMVector3Normalize(Towards(own.position, aim));
  const bool onTarget = XMVectorGetX(XMVector3Dot(nose, toAim)) >= std::cos(XMConvertToRadians(FIRE_CONE_DEGREES));
  command.wishes.fire = m_burstTicks > 0 && onTarget && distance <= _view.gunRangeMetres * m_pilot.rangeFactor;
  return command;
}

} // namespace Outpost
