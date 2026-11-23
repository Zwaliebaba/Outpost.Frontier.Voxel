#pragma once

#include "MaskEvent.h"
#include "Messages.h"
#include "SceneDefinition.h"
#include "UniverseSnapshot.h"

#include <DirectXMath.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Outpost
{

/// One object's voxel mask as the client holds it (plan task 3.9), and the cells that changed since
/// the surfaces last took them.
struct ClientMask
{
  ObjectId object = NO_OBJECT;
  std::size_t archetype = 0; // into CombatContent::archetypes
  Neuron::VoxelMask mask;
  std::vector<std::uint32_t> changed; // cells set or cleared since the surfaces last took them, which clears this
  bool whole = false;                 // the whole mask changed (a snapshot) since then
  std::uint64_t revision = 0;         // raised by every change
};

/// A bolt in flight, as the server said it left its muzzle.
struct ClientBolt
{
  std::uint32_t bolt = 0;
  ObjectId shooter = NO_OBJECT;
  UniversePosition origin;
  DirectX::XMFLOAT3 velocityMetresPerSecond{};
  std::uint64_t firedTick = 0;
  std::int32_t lifetimeTicks = 0;

  /// Where it is at a tick, fractional for a frame between ticks.
  [[nodiscard]] UniversePosition At(double _tick, std::uint32_t _ticksPerSecond) const noexcept;
};

/// The client's picture of combat (plan task 3.9): each damaged object's mask, kept from the
/// server's snapshots and events by ApplyMaskEvent, the function the server applies them with
/// (AGENTS.md R22); and the bolts in flight and the hits not yet shown, for presentation. An object
/// that nothing has changed has no mask here: it is still its scene instance's initial mask, which
/// its shared model draws.
class ClientCombat
{
public:
  /// At most this many hits wait to be shown; the oldest go first.
  static constexpr std::size_t MAX_PENDING_HITS = 512;

  void Load(std::shared_ptr<const CombatContent> _content, const SceneDefinition& _scene);
  [[nodiscard]] const CombatContent* Content() const noexcept
  {
    return m_content.get();
  }

  /// Takes a combat message: a mask snapshot, a damage event, a bolt fired or a bolt that hit. The
  /// universe is the client's picture, already brought up to date by the same message, which says
  /// what model, and so what archetype, an object has. False for any other kind of message.
  bool Apply(const Message& _message, const UniverseSnapshot& _universe);
  /// Drops what belongs to objects the universe no longer holds, and bolts whose lifetime is over.
  void Prune(const UniverseSnapshot& _universe);

  [[nodiscard]] const ClientMask* Find(ObjectId _object) const noexcept;
  [[nodiscard]] std::span<ClientMask> Masks() noexcept
  {
    return m_masks;
  }
  [[nodiscard]] std::span<const ClientMask> Masks() const noexcept
  {
    return m_masks;
  }
  [[nodiscard]] const std::vector<ClientBolt>& Bolts() const noexcept
  {
    return m_bolts;
  }
  /// The hits since the last call, oldest first.
  [[nodiscard]] std::vector<BoltHitMessage> TakeHits() noexcept;
  /// Events that did not follow their mask's last one: a defect in the stream, counted rather than
  /// guessed at. Snapshots repair them.
  [[nodiscard]] std::uint64_t OutOfOrderEvents() const noexcept
  {
    return m_outOfOrder;
  }

private:
  [[nodiscard]] ClientMask* Ensure(ObjectId _object, const UniverseSnapshot& _universe);
  void ApplySnapshot(const MaskSnapshotMessage& _snapshot, const UniverseSnapshot& _universe);
  void ApplyEvent(const MaskEvent& _event, const UniverseSnapshot& _universe);

  std::shared_ptr<const CombatContent> m_content;
  SceneDefinition m_scene;
  std::vector<ClientMask> m_masks;
  std::vector<ClientBolt> m_bolts;
  std::vector<BoltHitMessage> m_hits;
  std::uint64_t m_outOfOrder = 0;
};

} // namespace Outpost
