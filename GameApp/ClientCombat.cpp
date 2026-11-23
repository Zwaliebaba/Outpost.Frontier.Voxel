#include "ClientCombat.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Outpost
{

UniversePosition ClientBolt::At(double _tick, std::uint32_t _ticksPerSecond) const noexcept
{
  const double seconds = std::max(0.0, _tick - static_cast<double>(firedTick)) / static_cast<double>(_ticksPerSecond);
  return UniversePosition{origin.x + static_cast<double>(velocityMetresPerSecond.x) * seconds,
                          origin.y + static_cast<double>(velocityMetresPerSecond.y) * seconds,
                          origin.z + static_cast<double>(velocityMetresPerSecond.z) * seconds};
}

void ClientCombat::Load(std::shared_ptr<const CombatContent> _content, const SceneDefinition& _scene)
{
  m_content = std::move(_content);
  m_scene = _scene;
  m_masks.clear();
  m_bolts.clear();
  m_hits.clear();
  m_outOfOrder = 0;
}

bool ClientCombat::Apply(const Message& _message, const UniverseSnapshot& _universe)
{
  if (const auto* snapshot = std::get_if<MaskSnapshotMessage>(&_message))
  {
    ApplySnapshot(*snapshot, _universe);
    return true;
  }
  if (const auto* damage = std::get_if<DamageEventMessage>(&_message))
  {
    ApplyEvent(damage->event, _universe);
    return true;
  }
  if (const auto* fired = std::get_if<BoltFiredMessage>(&_message))
  {
    m_bolts.push_back(
      ClientBolt{fired->bolt, fired->shooter, fired->origin, fired->velocityMetresPerSecond, fired->tick, fired->lifetimeTicks});
    return true;
  }
  if (const auto* hit = std::get_if<BoltHitMessage>(&_message))
  {
    std::erase_if(m_bolts, [&](const ClientBolt& _bolt) { return _bolt.bolt == hit->bolt; });
    if (m_hits.size() >= MAX_PENDING_HITS)
    {
      m_hits.erase(m_hits.begin());
    }
    m_hits.push_back(*hit);
    return true;
  }
  return false;
}

void ClientCombat::Prune(const UniverseSnapshot& _universe)
{
  std::erase_if(m_masks, [&](const ClientMask& _mask) { return _universe.FindObject(_mask.object) == nullptr; });
  std::erase_if(m_bolts, [&](const ClientBolt& _bolt)
                { return static_cast<std::int64_t>(_universe.tick - _bolt.firedTick) > std::int64_t{_bolt.lifetimeTicks}; });
}

const ClientMask* ClientCombat::Find(ObjectId _object) const noexcept
{
  const auto found = std::ranges::find(m_masks, _object, &ClientMask::object);
  return found != m_masks.end() ? &*found : nullptr;
}

std::vector<BoltHitMessage> ClientCombat::TakeHits() noexcept
{
  return std::exchange(m_hits, {});
}

ClientMask* ClientCombat::Ensure(ObjectId _object, const UniverseSnapshot& _universe)
{
  const auto found = std::ranges::find(m_masks, _object, &ClientMask::object);
  if (found != m_masks.end())
  {
    return &*found;
  }
  const UniverseObject* object = _universe.FindObject(_object);
  const VoxelArchetype* archetype = object != nullptr && m_content != nullptr ? m_content->FindArchetype(object->model) : nullptr;
  if (archetype == nullptr)
  {
    return nullptr;
  }
  ClientMask mask;
  mask.object = _object;
  mask.archetype = static_cast<std::size_t>(archetype - m_content->archetypes.data());
  // Where the scene places the object, it starts as its instance's initial mask, as the server's
  // did; anything else (a wreck) starts empty and is told whole.
  const SceneInstance* instance = m_scene.FindInstance(object->name);
  mask.mask = instance != nullptr ? InitialMask(*m_content, *archetype, instance->preset) : Neuron::VoxelMask(archetype->full.Size());
  m_masks.push_back(std::move(mask));
  return &m_masks.back();
}

void ClientCombat::ApplySnapshot(const MaskSnapshotMessage& _snapshot, const UniverseSnapshot& _universe)
{
  ClientMask* held = Ensure(_snapshot.object, _universe);
  if (held == nullptr)
  {
    return;
  }
  const Neuron::VoxelMask& full = m_content->archetypes[held->archetype].full;
  Neuron::VoxelMask mask;
  if (_snapshot.reference == MaskReference::Full)
  {
    if (!Neuron::MaskFromAbsentSpans(full, _snapshot.spans, mask))
    {
      return;
    }
  }
  else
  {
    mask = Neuron::VoxelMask(full.Size());
    for (const Neuron::CellSpan& span : _snapshot.spans)
    {
      if (span.start >= mask.CellCount() || span.count > mask.CellCount() - span.start)
      {
        return;
      }
      for (std::uint32_t i = span.start; i < span.start + span.count; ++i)
      {
        mask.SetAt(i, full.PresentAt(i));
      }
    }
  }
  mask.SetLastEvent(_snapshot.lastEvent);
  held->mask = std::move(mask);
  held->whole = true;
  held->changed.clear();
  ++held->revision;
}

void ClientCombat::ApplyEvent(const MaskEvent& _event, const UniverseSnapshot& _universe)
{
  ClientMask* held = Ensure(_event.target, _universe);
  if (held == nullptr)
  {
    return;
  }
  const std::uint64_t sequence = _event.kind == MaskEventKind::Patch ? _event.patch.sequence : _event.carve.sequence;
  if (sequence <= held->mask.LastEvent())
  {
    return; // already in what the client holds
  }
  if (sequence != held->mask.LastEvent() + 1)
  {
    ++m_outOfOrder;
  }
  const MaskEventResult result = ApplyMaskEvent(m_content->archetypes[held->archetype], _event, held->mask);
  if (!held->whole)
  {
    held->changed.insert(held->changed.end(), result.removed.begin(), result.removed.end());
    held->changed.insert(held->changed.end(), result.restored.begin(), result.restored.end());
    for (const std::vector<std::uint32_t>& part : result.detached)
    {
      held->changed.insert(held->changed.end(), part.begin(), part.end());
    }
  }
  ++held->revision;
}

} // namespace Outpost
