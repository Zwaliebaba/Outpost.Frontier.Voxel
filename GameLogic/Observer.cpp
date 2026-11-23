#include "Observer.h"

#include <algorithm>

namespace Outpost
{

bool Observer::Publish(const Universe& _universe, Neuron::LoopbackTransport& _transport)
{
  const UniverseSnapshot& state = _universe.State();
  m_lastMessageCount = 0;
  bool ok = true;

  if (m_sentTick != state.tick)
  {
    ok = Send(TickMessage{state.tick}, _transport) && ok;
    m_sentTick = state.tick;
  }

  // What no longer exists goes first, so that nothing later names it.
  for (std::size_t i = 0; i < m_knownObjects.size();)
  {
    const ObjectId id = m_knownObjects[i].id;
    if (state.FindObject(id) != nullptr)
    {
      ++i;
      continue;
    }
    ok = Send(ObjectRemovedMessage{state.tick, id}, _transport) && ok;
    m_knownObjects.erase(m_knownObjects.begin() + static_cast<std::ptrdiff_t>(i));
    std::erase_if(m_sentShips, [id](const ShipState& _ship) { return _ship.object == id; });
    std::erase_if(m_sentMotions, [id](const ShipMotionState& _motion) { return _motion.object == id; });
    std::erase_if(m_sentMasks, [id](const SentMask& _mask) { return _mask.object == id; });
  }
  std::vector<ObjectId> spawned;
  for (const UniverseObject& object : state.objects)
  {
    UniverseObject* known = nullptr;
    for (UniverseObject& candidate : m_knownObjects)
    {
      if (candidate.id == object.id)
      {
        known = &candidate;
        break;
      }
    }
    if (known == nullptr)
    {
      ok = Send(ObjectSpawnMessage{object}, _transport) && ok;
      m_knownObjects.push_back(object);
      spawned.push_back(object.id);
    }
    else if (!(known->pose == object.pose))
    {
      ok = Send(ObjectStateMessage{state.tick, object.id, object.pose}, _transport) && ok;
      known->pose = object.pose;
      known->poseTick = state.tick;
    }
  }

  ok = PublishMasks(_universe, spawned, _transport) && ok;
  ok = PublishBolts(_universe, _transport) && ok;

  if (m_ownShip != NO_OBJECT)
  {
    const DockStatusState dock = _universe.DockStatus(m_ownShip);
    if (!m_sentDock.has_value() || !(*m_sentDock == dock))
    {
      ok = Send(DockStatusMessage{m_ownShip, dock}, _transport) && ok;
      m_sentDock = dock;
    }
    const FlightState* flight = _universe.Flight(m_ownShip);
    const bool behind = !m_ownShipSentTick.has_value() || _universe.DivergedAt(m_ownShip) > *m_ownShipSentTick;
    if (flight != nullptr && behind)
    {
      ok = Send(OwnShipMessage{state.tick, m_ownShip, *flight}, _transport) && ok;
      m_ownShipSentTick = state.tick;
    }
  }
  for (const ShipMotionState& motion : state.motions)
  {
    const auto sent = std::ranges::find(m_sentMotions, motion.object, &ShipMotionState::object);
    // The tick is not part of what changed: an engine doing the same thing is not sent again.
    ShipMotionState compared = motion;
    if (sent != m_sentMotions.end())
    {
      compared.tick = sent->tick;
    }
    if (sent == m_sentMotions.end() || !(compared == *sent))
    {
      ok = Send(ShipMotionMessage{motion}, _transport) && ok;
      if (sent == m_sentMotions.end())
      {
        m_sentMotions.push_back(motion);
      }
      else
      {
        *sent = motion;
      }
    }
  }
  for (const ShipState& ship : state.ships)
  {
    // Ships by their object, so that one removed does not make every ship after it look changed.
    const auto sent = std::ranges::find(m_sentShips, ship.object, &ShipState::object);
    if (sent == m_sentShips.end() || !(*sent == ship))
    {
      ok = Send(ShipMessage{ship}, _transport) && ok;
      if (sent == m_sentShips.end())
      {
        m_sentShips.push_back(ship);
      }
      else
      {
        *sent = ship;
      }
    }
  }
  for (std::size_t i = 0; i < state.objectives.size(); ++i)
  {
    if (i >= m_sentObjectives.size() || !(m_sentObjectives[i] == state.objectives[i]))
    {
      ok = Send(ObjectiveMessage{static_cast<std::uint16_t>(i), state.objectives[i]}, _transport) && ok;
      m_sentObjectives.resize(std::max(m_sentObjectives.size(), i + 1));
      m_sentObjectives[i] = state.objectives[i];
    }
  }
  for (std::size_t i = 0; i < state.resources.size(); ++i)
  {
    if (i >= m_sentResources.size() || !(m_sentResources[i] == state.resources[i]))
    {
      ok = Send(ResourceMessage{state.tick, static_cast<std::uint16_t>(i), state.resources[i]}, _transport) && ok;
      m_sentResources.resize(std::max(m_sentResources.size(), i + 1));
      m_sentResources[i] = state.resources[i];
    }
  }
  return ok;
}

MaskSnapshotMessage Observer::SnapshotOf(ObjectId _object, const Neuron::VoxelMask& _mask, const Neuron::VoxelMask& _full)
{
  MaskSnapshotMessage snapshot{_object, _mask.LastEvent(), MaskReference::Full, Neuron::AbsentSpans(_mask, _full)};
  std::vector<Neuron::CellSpan> present = Neuron::AbsentSpans(Neuron::VoxelMask(_mask.Size()), _mask);
  if (present.size() < snapshot.spans.size())
  {
    snapshot.reference = MaskReference::Empty;
    snapshot.spans = std::move(present);
  }
  return snapshot;
}

bool Observer::PublishMasks(const Universe& _universe, const std::vector<ObjectId>& _spawned, Neuron::LoopbackTransport& _transport)
{
  const Combat& combat = _universe.Battle();
  bool ok = true;
  // A newly told object: the client starts it at its instance's initial mask, so it is sent whole
  // only when it is no longer that.
  for (const ObjectId object : _spawned)
  {
    const Neuron::VoxelMask* mask = combat.Mask(object);
    const Neuron::VoxelMask* initial = combat.Initial(object);
    const VoxelArchetype* archetype = combat.ArchetypeOf(object);
    if (mask == nullptr || initial == nullptr || archetype == nullptr)
    {
      continue;
    }
    if (*mask == *initial && mask->LastEvent() == 0)
    {
      m_sentMasks.push_back(SentMask{object, 0, combat.MaskRevision(object)});
      continue;
    }
    ok = Send(SnapshotOf(object, *mask, archetype->full), _transport) && ok;
    m_sentMasks.push_back(SentMask{object, mask->LastEvent(), combat.MaskRevision(object)});
  }
  // Events in the order they were issued, each the next on its object's mask.
  const std::vector<MaskEvent>& events = combat.Events();
  const std::uint64_t firstLogged = combat.EventsLogged() - events.size();
  for (std::uint64_t n = std::max(m_eventsSeen, firstLogged); n < combat.EventsLogged(); ++n)
  {
    const MaskEvent& event = events[static_cast<std::size_t>(n - firstLogged)];
    const auto sent = std::ranges::find(m_sentMasks, event.target, &SentMask::object);
    const std::uint64_t sequence = event.kind == MaskEventKind::Patch ? event.patch.sequence : event.carve.sequence;
    if (sent == m_sentMasks.end() || sequence != sent->lastEvent + 1 || sent->revision != combat.MaskRevision(event.target))
    {
      continue; // not told of the object, or the snapshot below tells it
    }
    ok = Send(DamageEventMessage{event}, _transport) && ok;
    sent->lastEvent = sequence;
  }
  m_eventsSeen = combat.EventsLogged();
  // Whatever events cannot bring up to date goes whole: a mask restored, a ship returned to base, an
  // event the log no longer holds.
  for (SentMask& sent : m_sentMasks)
  {
    const Neuron::VoxelMask* mask = combat.Mask(sent.object);
    const VoxelArchetype* archetype = combat.ArchetypeOf(sent.object);
    if (mask == nullptr || archetype == nullptr ||
        (mask->LastEvent() == sent.lastEvent && combat.MaskRevision(sent.object) == sent.revision))
    {
      continue;
    }
    ok = Send(SnapshotOf(sent.object, *mask, archetype->full), _transport) && ok;
    sent.lastEvent = mask->LastEvent();
    sent.revision = combat.MaskRevision(sent.object);
  }
  return ok;
}

bool Observer::PublishBolts(const Universe& _universe, Neuron::LoopbackTransport& _transport)
{
  const Combat& combat = _universe.Battle();
  bool ok = true;
  const std::vector<BoltFiredMessage>& fired = combat.Fired();
  const std::uint64_t firstFired = combat.FiredLogged() - fired.size();
  for (std::uint64_t n = std::max(m_firedSeen, firstFired); n < combat.FiredLogged(); ++n)
  {
    ok = Send(fired[static_cast<std::size_t>(n - firstFired)], _transport) && ok;
  }
  m_firedSeen = combat.FiredLogged();
  const std::vector<BoltHitMessage>& hits = combat.Hits();
  const std::uint64_t firstHit = combat.HitsLogged() - hits.size();
  for (std::uint64_t n = std::max(m_hitsSeen, firstHit); n < combat.HitsLogged(); ++n)
  {
    ok = Send(hits[static_cast<std::size_t>(n - firstHit)], _transport) && ok;
  }
  m_hitsSeen = combat.HitsLogged();
  return ok;
}

bool Observer::Send(const Message& _message, Neuron::LoopbackTransport& _transport)
{
  const std::vector<std::uint8_t> bytes = Encode(_message);
  if (bytes.empty())
  {
    return false;
  }
  const Route route = RouteOf(KindOf(_message));
  ++m_lastMessageCount;
  return _transport.Send(Neuron::Peer::Server, route.delivery, route.stream, bytes);
}

} // namespace Outpost
