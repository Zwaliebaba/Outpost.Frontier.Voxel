#include "World.h"

#include "TickClock.h"

#include <algorithm>

namespace Outpost
{

void World::Load(const SceneDefinition& _scene)
{
  m_snapshot = WorldSnapshot{};
  for (const ShipDefinition& ship : _scene.ships)
  {
    m_snapshot.ships.push_back(ShipState{ship.instance, ship.callsign, ship.shipClass, ship.hullPercent, ship.modules});
  }
  m_snapshot.objectives = _scene.objectives;
  m_income.clear();
  for (const ResourceDefinition& resource : _scene.resources)
  {
    m_snapshot.resources.push_back(ResourceState{resource.name, resource.amount, resource.capacity});
    m_income.push_back(Income{resource.perMinute, 0});
  }
}

void World::Tick()
{
  // Integer income: perMinute is spread over the minute's ticks with the remainder carried, so
  // the total after a minute is exact and the result never depends on frame timing.
  constexpr std::int64_t TICKS_PER_MINUTE = std::int64_t{Neuron::TickClock::TICKS_PER_SECOND} * 60;
  ++m_snapshot.tick;
  for (std::size_t i = 0; i < m_income.size(); ++i)
  {
    Income& income = m_income[i];
    income.remainder += income.perMinute;
    const std::int64_t whole = income.remainder / TICKS_PER_MINUTE;
    income.remainder -= whole * TICKS_PER_MINUTE;
    ResourceState& resource = m_snapshot.resources[i];
    resource.amount = std::max<std::int64_t>(0, resource.amount + whole);
    if (resource.capacity > 0)
    {
      resource.amount = std::min(resource.amount, resource.capacity);
    }
  }
}

} // namespace Outpost
