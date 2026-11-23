#include "Launcher.h"

#include "FlightModel.h"

#include <cmath>

using namespace DirectX;

namespace Outpost
{

FireResult StepLaunchers(const ShipFlightSpec& _spec, bool _fire, const UniversePosition& _aim, std::uint32_t _ticksPerSecond,
                         FlightState& _state) noexcept
{
  FireResult result;
  const XMVECTOR orientation = XMLoadFloat4(&_state.orientation);
  const XMVECTOR nose = NoseDirection(_state);
  const float aimLimitCosine = std::cos(XMConvertToRadians(GUN_AIM_LIMIT_DEGREES));
  for (std::size_t i = 0; i < MAX_LAUNCHERS; ++i)
  {
    const LauncherMount& mount = _spec.launchers[i];
    std::int32_t& clock = _state.launcherClockTicks[i];
    clock = clock < LAUNCHER_CLOCK_READY_TICKS ? clock + 1 : clock;
    if (!mount.fitted || !_fire)
    {
      continue;
    }
    FireOutcome& outcome = result.outcomes[i];
    const auto refireTicks = static_cast<std::int32_t>(std::lround(mount.refireSeconds * static_cast<float>(_ticksPerSecond)));
    if (!mount.working)
    {
      outcome = FireOutcome::Disabled;
      continue;
    }
    if (clock < refireTicks)
    {
      outcome = FireOutcome::Refire;
      continue;
    }
    clock = 0;
    if (_state.powerPool < mount.powerPerShot)
    {
      outcome = FireOutcome::Power;
      continue;
    }
    XMFLOAT3 muzzle{};
    XMStoreFloat3(&muzzle, XMVector3Rotate(XMLoadFloat3(&mount.muzzleModel), orientation));
    const UniversePosition origin{_state.position.x + static_cast<double>(muzzle.x), _state.position.y + static_cast<double>(muzzle.y),
                                  _state.position.z + static_cast<double>(muzzle.z)};
    const XMVECTOR toAim = XMVectorSet(static_cast<float>(_aim.x - origin.x), static_cast<float>(_aim.y - origin.y),
                                       static_cast<float>(_aim.z - origin.z), 0.0f);
    const float length = XMVectorGetX(XMVector3Length(toAim));
    const XMVECTOR direction = length > 1.0e-3f ? XMVectorScale(toAim, 1.0f / length) : nose;
    if (XMVectorGetX(XMVector3Dot(direction, nose)) < aimLimitCosine)
    {
      outcome = FireOutcome::AimLimit;
      continue;
    }
    if (_state.cruise == CruiseState::Cruising)
    {
      outcome = FireOutcome::Cruise;
      continue;
    }
    _state.powerPool -= mount.powerPerShot;
    outcome = FireOutcome::Fired;
    Shot& shot = result.shots[result.shotCount++];
    shot.mount = static_cast<std::uint8_t>(i);
    shot.origin = origin;
    XMStoreFloat3(&shot.direction, direction);
  }
  return result;
}

std::optional<float> LeadSeconds(const XMFLOAT3& _offset, const XMFLOAT3& _velocity, float _muzzleSpeedMetresPerSecond) noexcept
{
  // |p + v t|^2 = s^2 t^2:  (v.v - s^2) t^2 + 2 (p.v) t + p.p = 0.
  const XMVECTOR p = XMLoadFloat3(&_offset);
  const XMVECTOR v = XMLoadFloat3(&_velocity);
  const float a = XMVectorGetX(XMVector3Dot(v, v)) - _muzzleSpeedMetresPerSecond * _muzzleSpeedMetresPerSecond;
  const float b = 2.0f * XMVectorGetX(XMVector3Dot(p, v));
  const float c = XMVectorGetX(XMVector3Dot(p, p));
  if (std::abs(a) < 1.0e-6f)
  {
    if (std::abs(b) < 1.0e-6f)
    {
      return std::nullopt;
    }
    const float t = -c / b;
    return t > 0.0f ? std::optional<float>(t) : std::nullopt;
  }
  const float discriminant = b * b - 4.0f * a * c;
  if (discriminant < 0.0f)
  {
    return std::nullopt;
  }
  const float root = std::sqrt(discriminant);
  const float t0 = (-b - root) / (2.0f * a);
  const float t1 = (-b + root) / (2.0f * a);
  const float smaller = std::min(t0, t1);
  const float larger = std::max(t0, t1);
  if (smaller > 0.0f)
  {
    return smaller;
  }
  return larger > 0.0f ? std::optional<float>(larger) : std::nullopt;
}

} // namespace Outpost
