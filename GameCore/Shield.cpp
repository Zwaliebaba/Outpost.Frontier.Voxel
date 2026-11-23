#include "Shield.h"

#include "FlightModel.h"

#include <algorithm>
#include <cmath>

namespace Outpost
{

void StepShield(const ShipFlightSpec& _spec, std::uint32_t _ticksPerSecond, FlightState& _state) noexcept
{
  const ShieldSpec& shield = _spec.shield;
  if (shield.capacity <= 0.0f)
  {
    return;
  }
  const float dt = 1.0f / static_cast<float>(_ticksPerSecond);
  if (_state.shield < shield.capacity)
  {
    const float draw = shield.drawPerSecond * dt;
    if (_state.powerPool >= draw && shield.regenerationPerSecond > 0.0f)
    {
      _state.powerPool -= draw;
      _state.shield = std::min(shield.capacity, _state.shield + shield.regenerationPerSecond * dt);
    }
  }
  if (!_state.shieldUp)
  {
    _state.shieldRebuildTicks = std::max(0, _state.shieldRebuildTicks - 1);
    if (_state.shieldRebuildTicks == 0 && _state.shield > shield.offlineShare * shield.capacity)
    {
      _state.shieldUp = true;
    }
  }
}

bool DamageShield(const ShipFlightSpec& _spec, std::uint32_t _ticksPerSecond, float _damage, FlightState& _state) noexcept
{
  const ShieldSpec& shield = _spec.shield;
  _state.shield = std::max(0.0f, _state.shield - std::max(0.0f, _damage));
  if (_state.shieldUp && _state.shield <= shield.offlineShare * shield.capacity)
  {
    _state.shieldUp = false;
    _state.shieldRebuildTicks = static_cast<std::int32_t>(std::lround(shield.rebuildSeconds * static_cast<float>(_ticksPerSecond)));
    return true;
  }
  return false;
}

} // namespace Outpost
