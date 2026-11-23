#pragma once

#include <cstdint>
#include <string>

namespace Outpost
{

struct ShipFlightSpec;
struct FlightState;

// Shields (plan task 3.5; blueprint ch. 03 §3.5): one bubble around a ship, filled by its generator
// from the power pool. Nothing passes through it; at the offline threshold it collapses and stays
// down for the rebuild time. Its state is part of the ship's flight state, stepped in the shared
// ship tick, so the client's prediction regenerates it, and pays for it, as the server does.

/// A shield generator as authored (Assets/Content/Ships.txt).
struct ShieldArchetype
{
  std::string name;
  float capacity = 0.0f;
  float regenerationPerSecond = 0.0f;
  float drawPerSecond = 0.0f;  // taken from the power pool while it regenerates
  float offlineShare = 0.15f;  // it collapses at or below this share of its capacity
  float rebuildSeconds = 0.0f; // and comes back no sooner than this after collapsing
};

/// A shield as a ship's spec carries it.
struct ShieldSpec
{
  float capacity = 0.0f; // 0: no shield
  float regenerationPerSecond = 0.0f;
  float drawPerSecond = 0.0f;
  float offlineShare = 0.15f;
  float rebuildSeconds = 0.0f;
};

/// One tick of the generator (ch. 03 §3.5): while below full, if the pool holds the tick's draw it
/// pays it and the shield regenerates, down or not; a collapsed shield comes back once its rebuild
/// time is over and it is above the threshold. The bubble's "nothing inside" test is not modelled.
void StepShield(const ShipFlightSpec& _spec, std::uint32_t _ticksPerSecond, FlightState& _state) noexcept;

/// A hit on the bubble: the shield loses _damage, never below nothing, and nothing passes through.
/// True when the hit collapsed it.
bool DamageShield(const ShipFlightSpec& _spec, std::uint32_t _ticksPerSecond, float _damage, FlightState& _state) noexcept;

/// What a munition takes from a shield (ch. 03 §3.4): half its hull damage and all its energy
/// damage. No weapon or shield types exist yet, so the modifier is 1.
[[nodiscard]] constexpr float ShieldDamage(float _hullDamage, float _energyDamage) noexcept
{
  return _hullDamage * 0.5f + _energyDamage;
}

} // namespace Outpost
