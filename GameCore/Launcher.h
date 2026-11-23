#pragma once

#include "UniverseObject.h"
#include "VoxelCarve.h"

#include <DirectXMath.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Outpost
{

struct ShipFlightSpec;
struct FlightState;

// Launchers (plan task 3.4; blueprint ch. 03 §3.2): fixed guns on a ship's weapon mounts, one
// firing cycle each. The cycle runs inside the shared ship tick (ShipFlight.h), so the client's
// prediction pays for its own shots exactly as the server does; only the server makes bolts.

inline constexpr std::size_t MAX_LAUNCHERS = 4;
/// A gun refuses an aim point further than this off its barrel, which is the ship's nose (ch. 03 §3.2, test 7).
inline constexpr float GUN_AIM_LIMIT_DEGREES = 40.0f;
/// A launcher's clock starts this far past any refire delay, so that a new ship can fire at once.
inline constexpr std::int32_t LAUNCHER_CLOCK_READY_TICKS = 1'000'000;

/// A gun and its bolt as authored (Assets/Content/Ships.txt).
struct LauncherArchetype
{
  std::string name;
  float refireSeconds = 0.0f;
  float powerPerShot = 0.0f;
  float muzzleSpeedMetresPerSecond = 0.0f;
  float lifetimeSeconds = 0.0f;
  float hullDamage = 0.0f;   // what a hit takes from a shield, halved (ch. 03 §3.4), and the hit's weight
  float energyDamage = 0.0f; // what a hit takes from the target's power pool, or its shield in full
  // The carve a hit on voxels makes (ADR-012): its energy in strength units, its shape and size.
  std::uint32_t carveEnergy = 0;
  Neuron::CarveShape carveShape = Neuron::CarveShape::Sphere;
  std::uint8_t carveRadiusCells = 1;
  std::uint8_t carveLengthCells = 1;
};

/// A launcher as a ship's spec carries it: the archetype's cycle, and where its muzzle is.
struct LauncherMount
{
  bool fitted = false;
  bool working = true;        // false once its weapon module has stopped (ModuleRegion.h)
  std::uint8_t archetype = 0; // the launcher's index in ShipContent::launchers
  float refireSeconds = 0.0f;
  float powerPerShot = 0.0f;
  DirectX::XMFLOAT3 muzzleModel{}; // the muzzle, as an offset from the pivot in the ship's frame
};

/// Why a launcher did not fire this tick, in the order the blueprint tests (ch. 03 §3.2); ammunition,
/// cloaks and lanes do not exist yet.
enum class FireOutcome : std::uint8_t
{
  Idle,     // not asked to fire, or not fitted
  Disabled, // test 1: its module has stopped
  Refire,   // test 2: its clock is below the refire delay
  Power,    // test 3: the pool holds less than a shot
  AimLimit, // test 7: the aim point is more than 40 degrees off the barrel
  Cruise,   // test 8: cruise is engaged
  Fired
};

struct Shot
{
  std::uint8_t mount = 0;
  UniversePosition origin;
  DirectX::XMFLOAT3 direction{0.0f, 0.0f, 1.0f}; // unit, world frame: from the muzzle to the aim point
};

struct FireResult
{
  std::array<FireOutcome, MAX_LAUNCHERS> outcomes{};
  std::array<Shot, MAX_LAUNCHERS> shots{};
  std::uint8_t shotCount = 0;
};

/// One tick of every launcher's cycle: the clocks count up, and if _fire is asked each fitted
/// launcher refuses with the first reason that applies or fires one shot at _aim. The clock restarts
/// on every attempt that passed the refire test, so trying to fire on an empty pool still costs a
/// cycle. A shot leaves from its muzzle towards the aim point, so the guns converge on it.
FireResult StepLaunchers(const ShipFlightSpec& _spec, bool _fire, const UniversePosition& _aim, std::uint32_t _ticksPerSecond,
                         FlightState& _state) noexcept;

/// The time after which a bolt at muzzle speed meets a target at relative position _offset moving
/// at relative velocity _velocity: the smallest positive root of |p + v t| = s t (ch. 03 §3.2).
[[nodiscard]] std::optional<float> LeadSeconds(const DirectX::XMFLOAT3& _offset, const DirectX::XMFLOAT3& _velocity,
                                               float _muzzleSpeedMetresPerSecond) noexcept;

} // namespace Outpost
