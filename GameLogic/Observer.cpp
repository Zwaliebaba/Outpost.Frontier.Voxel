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
    }
    else if (!(known->pose == object.pose))
    {
      ok = Send(ObjectStateMessage{state.tick, object.id, object.pose}, _transport) && ok;
      known->pose = object.pose;
      known->poseTick = state.tick;
    }
  }

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
  for (std::size_t i = 0; i < state.ships.size(); ++i)
  {
    if (i >= m_sentShips.size() || !(m_sentShips[i] == state.ships[i]))
    {
      ok = Send(ShipMessage{state.ships[i]}, _transport) && ok;
      m_sentShips.resize(std::max(m_sentShips.size(), i + 1));
      m_sentShips[i] = state.ships[i];
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
