#pragma once

#include "FlightModel.h"
#include "Launcher.h"
#include "MaskEvent.h"
#include "SceneDefinition.h"
#include "ShipArchetype.h"
#include "UniverseObject.h"

#include <DirectXMath.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Outpost
{

/// Something a bolt can meet this tick, as the universe sees it.
struct CombatTarget
{
  ObjectId object = NO_OBJECT;
  Pose pose;
  float scale = 1.0f;
  Owner owner = Owner::Neutral;
  bool isProtected = false;             // carried or docked (GameDesign.md D16): nothing harms it
  FlightState* flight = nullptr;        // a ship's, for its shield; null for anything else
  const ShipFlightSpec* spec = nullptr; // with it
  float shieldRadiusMetres = 0.0f;      // the bubble: the sphere the ship sweeps as
};

/// What a bolt did when it met something (plan task 3.6).
struct BoltHit
{
  ObjectId shooter = NO_OBJECT;
  ObjectId target = NO_OBJECT;
  bool shield = false;  // it met the bubble, and only the shield is touched
  bool harmful = false; // false: it stopped there and did nothing (D2, D16, D24)
  float shieldDamage = 0.0f;
  float energyDamage = 0.0f; // from the target's power pool, when it met the hull
  MaskEventResult carved;    // what the carve took, when it met voxels and was harmful
};

/// Voxels cut loose from a ship, drifting (plan task 3.7). Server-side only until the client can draw
/// a mask (plan task 3.9): until then the observer does not publish them.
struct Wreck
{
  ObjectId parent = NO_OBJECT;
  std::size_t archetype = 0;
  Neuron::VoxelMask mask;
  Pose pose;
  float scale = 1.0f;
  DirectX::XMFLOAT3 velocityMetresPerSecond{};
};

/// Whether a bolt from _shooter harms what it meets, owned by _target (GameDesign.md D24): a player's
/// bolts harm only what is hostile; anyone else's harm whatever is not their own.
[[nodiscard]] constexpr bool Harms(Owner _shooter, Owner _target) noexcept
{
  return _shooter == Owner::Player ? _target == Owner::Hostile : _target != _shooter;
}

/// The authority's side of combat (plan tasks 3.1, 3.4, 3.6, 3.7, 3.11): every damageable object's
/// mask against its archetype, the bolts in flight, the wrecks, and the log of every mask event it
/// issued. Bolts are traced against shield bubbles, then through masks cell by cell; a harmful hit on
/// voxels becomes a mask event, applied with ApplyMaskEvent, the function the client applies it with.
class Combat
{
public:
  /// A part cut loose drifts off at its ship's velocity plus this, away from the ship's pivot.
  static constexpr float WRECK_KICK_METRES_PER_SECOND = 2.0f;
  /// The log keeps at most this many events, dropping the oldest.
  static constexpr std::size_t MAX_LOGGED_EVENTS = std::size_t{1} << 16;

  /// One body per object whose model the content has an archetype for, each at its initial mask.
  /// Without content the universe has no combat: no bodies, and bolts meet only shields.
  void Load(std::shared_ptr<const CombatContent> _content, const SceneDefinition& _scene, const std::vector<UniverseObject>& _objects);
  [[nodiscard]] bool Active() const noexcept
  {
    return m_content != nullptr;
  }
  [[nodiscard]] const CombatContent* Content() const noexcept
  {
    return m_content.get();
  }

  /// A shot becomes a bolt at its muzzle, moving at the launcher's speed plus the shooter's velocity,
  /// for the launcher's lifetime. _launcher indexes ShipContent::launchers.
  void Fire(ObjectId _shooter, Owner _owner, const Shot& _shot, std::uint8_t _launcher, const ShipContent& _ships,
            const DirectX::XMFLOAT3& _shooterVelocity, std::uint32_t _ticksPerSecond, std::uint64_t _tick);
  /// Every bolt advances a tick and is traced against the targets; the first thing it meets ends it.
  /// Hits on voxels are applied here; hits on shields and pools are the caller's, from the result.
  [[nodiscard]] std::vector<BoltHit> StepBolts(std::span<const CombatTarget> _targets, const ShipContent& _ships,
                                               std::uint32_t _ticksPerSecond, std::uint64_t _tick);
  void StepWrecks(std::uint32_t _ticksPerSecond);

  [[nodiscard]] const Neuron::VoxelMask* Mask(ObjectId _object) const noexcept;
  /// The mask the object started with.
  [[nodiscard]] const Neuron::VoxelMask* Initial(ObjectId _object) const noexcept;
  [[nodiscard]] const VoxelArchetype* ArchetypeOf(ObjectId _object) const noexcept;
  /// Each module region's health, measured from the mask, in the hull's order; empty without a hull.
  [[nodiscard]] std::vector<ShipModuleState> Modules(ObjectId _object) const;
  /// The share of the hull's structure cells present, in whole percent (GameDesign.md §7.1).
  [[nodiscard]] int HullPercent(ObjectId _object) const;
  [[nodiscard]] int CoreHealthPercent(ObjectId _object) const;
  /// Whether the object's core has stopped, by Modules.txt's threshold: the ship is destroyed.
  [[nodiscard]] bool CoreStopped(ObjectId _object) const;

  /// Replaces an object's mask, as a save or a return to base (D1) restores it. False for an object
  /// with no body or a mask of another size.
  bool SetMask(ObjectId _object, const Neuron::VoxelMask& _mask);
  /// The ship breaks up: every cell it has left becomes wrecks, by connectivity, and its body goes.
  void BreakUp(ObjectId _object, const Pose& _pose, float _scale, const DirectX::XMFLOAT3& _velocity);
  /// As BreakUp, but the body stays, with no cells: a player's ship, which returns to its base (D1).
  void Shatter(ObjectId _object, const Pose& _pose, float _scale, const DirectX::XMFLOAT3& _velocity);
  /// The object no longer exists: its body goes, and the bolts it fired fly on.
  void Remove(ObjectId _object);
  /// A field repair (plan task 3.11): a patch event restoring up to _maxCells structure cells. The
  /// cells restored.
  std::size_t Patch(ObjectId _object, std::uint32_t _maxCells, std::uint64_t _tick);

  [[nodiscard]] const std::vector<MaskEvent>& Events() const noexcept
  {
    return m_events;
  }
  [[nodiscard]] const std::vector<Wreck>& Wrecks() const noexcept
  {
    return m_wrecks;
  }
  [[nodiscard]] std::size_t BoltCount() const noexcept
  {
    return m_bolts.size();
  }

private:
  struct Body
  {
    ObjectId object = NO_OBJECT;
    std::size_t archetype = 0;
    Neuron::VoxelMask mask;
    Neuron::VoxelMask initial;
    Owner owner = Owner::Neutral;
    bool indestructible = false;
    std::uint64_t nextEvent = 1;
    DirectX::XMFLOAT3 pivotVox{0.0f, 0.0f, 0.0f};
    float voxelSize = 1.0f;
  };

  struct Bolt
  {
    ObjectId shooter = NO_OBJECT;
    Owner owner = Owner::Neutral;
    std::uint8_t launcher = 0;
    UniversePosition position;
    DirectX::XMFLOAT3 velocityMetresPerSecond{};
    std::int32_t ticksLeft = 0;
    std::uint32_t seed = 0;
  };

  [[nodiscard]] Body* FindBody(ObjectId _object) noexcept;
  [[nodiscard]] const Body* FindBody(ObjectId _object) const noexcept;
  [[nodiscard]] const HullRecord* HullOf(const Body& _body) const noexcept;
  void Log(const MaskEvent& _event);
  void SpawnWrecks(const Body& _body, const std::vector<std::vector<std::uint32_t>>& _parts, const Pose& _pose, float _scale,
                   const DirectX::XMFLOAT3& _velocity);

  std::shared_ptr<const CombatContent> m_content;
  std::vector<Body> m_bodies;
  std::vector<Bolt> m_bolts;
  std::vector<Wreck> m_wrecks;
  std::vector<MaskEvent> m_events;
};

/// Where a segment, from _start along _direction (unit) for _lengthMetres, first meets a present cell
/// of a mask placed at _pose and _scale with the model's pivot and voxel size. The distance along the
/// segment and the cell, or nothing. The authority's only float work on a mask (ADR-012): it turns a
/// world-space hit into a cell, which the integer event then names.
struct CellHit
{
  float distanceMetres = 0.0f;
  Neuron::Int3 cell{0, 0, 0};
  DirectX::XMFLOAT3 directionVox{0.0f, 1.0f, 0.0f}; // the segment's direction in VOX axes, unit
};
[[nodiscard]] std::optional<CellHit> TraceMask(const Neuron::VoxelMask& _mask, const Pose& _pose, float _scale,
                                               const DirectX::XMFLOAT3& _pivotVox, float _voxelSize, const UniversePosition& _start,
                                               const DirectX::XMFLOAT3& _direction, float _lengthMetres) noexcept;

} // namespace Outpost
