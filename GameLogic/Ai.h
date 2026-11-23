#pragma once

#include "FlightModel.h"
#include "Pilot.h"
#include "Random.h"
#include "ShipFlight.h"
#include "UniverseObject.h"

#include <DirectXMath.h>

#include <cstdint>
#include <span>

namespace Outpost
{

/// A ship the pilot can see, as the universe states it this tick.
struct AiContact
{
  ObjectId object = NO_OBJECT;
  Owner owner = Owner::Neutral;
  UniversePosition position;
  DirectX::XMFLOAT3 velocityMetresPerSecond{};
  DirectX::XMFLOAT4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
  bool attackable = false; // flying, and neither carried nor docked
};

/// What the pilot knows of its own ship this tick.
struct AiView
{
  const FlightState* own = nullptr;
  const ShipFlightSpec* spec = nullptr;
  ObjectId self = NO_OBJECT;
  Owner enemy = Owner::Player; // whose ships it attacks: the owner tag stands in for factions (plan section 11)
  std::span<const AiContact> contacts;
  int corePercent = 100;
  int hullPercent = 100;
  float gunRangeMetres = 0.0f;
  float muzzleSpeedMetresPerSecond = 0.0f;
  std::uint32_t ticksPerSecond = 60;
};

enum class AiState : std::uint8_t
{
  Idle,   // no target: hold where it is
  RunIn,  // the attack pass (ch. 05 §3.4): face the target at full throttle
  Pass,   // fly straight by
  Extend, // fly away until far enough to turn in again
  Trail,  // behind a moving target: hold a distance and keep the nose on it
  Evade,  // an enemy close behind: dodge
  Flee,   // run from the target until gone (§3.9)
  Gone    // far enough from every player: the universe removes it
};

/// One NPC's pilot (plan task 3.8; blueprint ch. 05): picks the nearest enemy ship it can attack,
/// flies the attack pass, trails, evades and flees by its pilot record, and fires in bursts at the
/// lead point with an aim error of its own. Every random choice comes from a generator seeded with
/// the ship's identity, so a run repeats exactly. Each tick it turns what it sees into the command a
/// player's input would make, flown through the same manoeuvre layer.
class Ai
{
public:
  /// The cone within which a pilot's guns fire: the nose within this of the aim point (ch. 05 §3.8).
  static constexpr float FIRE_CONE_DEGREES = 3.0f;
  /// A target slower than this is aimed at without error (§3.8).
  static constexpr float STILL_TARGET_METRES_PER_SECOND = 3.0f;
  /// A target this fast or faster, with the pilot in its rear hemisphere, is trailed (§3.4).
  static constexpr float TRAIL_SPEED_METRES_PER_SECOND = 20.0f;

  Ai() = default;
  Ai(const PilotRecord& _pilot, std::uint64_t _seed) noexcept;

  [[nodiscard]] ShipCommand Think(const AiView& _view);

  [[nodiscard]] AiState State() const noexcept
  {
    return m_state;
  }
  [[nodiscard]] ObjectId Target() const noexcept
  {
    return m_target;
  }

private:
  [[nodiscard]] std::int32_t Ticks(float _seconds, std::uint32_t _ticksPerSecond) const noexcept;
  [[nodiscard]] std::int32_t Jittered(float _seconds, std::uint32_t _ticksPerSecond) noexcept;
  void Enter(AiState _state, std::int32_t _ticks) noexcept;
  [[nodiscard]] UniversePosition AimPoint(const AiView& _view, const AiContact& _target, float _distance) noexcept;

  PilotRecord m_pilot;
  Neuron::Pcg32 m_random{0};
  AiState m_state = AiState::Idle;
  ObjectId m_target = NO_OBJECT;
  std::int32_t m_stateTicks = 0;               // left in a timed state
  std::int32_t m_burstTicks = 0;               // positive: firing for this long; negative: resting
  std::int32_t m_rerollTicks = 0;              // until the aim error is drawn again
  DirectX::XMFLOAT2 m_errorAngles{0.0f, 0.0f}; // the aim error: its size and the direction across the line of sight
  DirectX::XMFLOAT3 m_dodge{0.0f, 0.0f, 1.0f};
  UniversePosition m_anchor;
  bool m_anchored = false;
};

} // namespace Outpost
