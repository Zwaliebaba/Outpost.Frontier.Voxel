#include "World.h"

#include "TickClock.h"

#include <algorithm>

namespace Outpost
{

bool World::LoadContent(const std::filesystem::path& _assetRoot, std::string& _outError)
{
  SceneDefinition scene;
  if (!LoadSceneContent(_assetRoot, scene, _outError))
  {
    return false;
  }
  Load(scene);
  return true;
}

void World::Load(const SceneDefinition& _scene)
{
  m_state = WorldSnapshot{};
  // Authored objects take identifiers in placement order, so the same content always gives the
  // same identifiers (ADR-009).
  for (std::size_t i = 0; i < _scene.instances.size(); ++i)
  {
    m_state.objects.push_back(ObjectFromInstance(_scene, i, static_cast<ObjectId>(i + 1)));
  }
  for (const ShipDefinition& ship : _scene.ships)
  {
    const int index = _scene.InstanceIndex(ship.instance);
    const ObjectId object = index >= 0 ? static_cast<ObjectId>(index + 1) : NO_OBJECT;
    m_state.ships.push_back(ShipState{ship.instance, ship.callsign, ship.shipClass, ship.hullPercent, ship.modules, object});
  }
  m_state.objectives = _scene.objectives;
  m_income.clear();
  for (const ResourceDefinition& resource : _scene.resources)
  {
    m_state.resources.push_back(ResourceState{resource.name, resource.amount, resource.capacity});
    m_state.resourceTicks.push_back(0);
    m_income.push_back(Income{resource.perMinute, 0});
  }
}

void World::Tick()
{
  // Integer income: perMinute is spread over the minute's ticks with the remainder carried, so
  // the total after a minute is exact and the result never depends on frame timing.
  constexpr std::int64_t TICKS_PER_MINUTE = std::int64_t{Neuron::TickClock::TICKS_PER_SECOND} * 60;
  ++m_state.tick;
  for (std::size_t i = 0; i < m_income.size(); ++i)
  {
    Income& income = m_income[i];
    income.remainder += income.perMinute;
    const std::int64_t whole = income.remainder / TICKS_PER_MINUTE;
    income.remainder -= whole * TICKS_PER_MINUTE;
    ResourceState& resource = m_state.resources[i];
    const std::int64_t before = resource.amount;
    resource.amount = std::max<std::int64_t>(0, resource.amount + whole);
    if (resource.capacity > 0)
    {
      resource.amount = std::min(resource.amount, resource.capacity);
    }
    if (resource.amount != before)
    {
      m_state.resourceTicks[i] = m_state.tick;
    }
  }
}

} // namespace Outpost
