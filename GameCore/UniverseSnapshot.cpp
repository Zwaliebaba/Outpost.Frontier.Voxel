#include "UniverseSnapshot.h"

#include <algorithm>

namespace Outpost
{

std::vector<ShipModuleState> ShipState::DamagedModules() const
{
  std::vector<ShipModuleState> damaged;
  for (const ShipModuleState& module : modules)
  {
    if (module.healthPercent < 100)
    {
      damaged.push_back(module);
    }
  }
  std::ranges::stable_sort(damaged,
                           [](const ShipModuleState& _a, const ShipModuleState& _b) { return _a.healthPercent < _b.healthPercent; });
  return damaged;
}

const ShipState* UniverseSnapshot::FindShip(std::string_view _instance) const noexcept
{
  for (const ShipState& ship : ships)
  {
    if (ship.instance == _instance)
    {
      return &ship;
    }
  }
  return nullptr;
}

const UniverseObject* UniverseSnapshot::FindObject(ObjectId _id) const noexcept
{
  for (const UniverseObject& object : objects)
  {
    if (object.id == _id)
    {
      return &object;
    }
  }
  return nullptr;
}

UniverseObject* UniverseSnapshot::FindObject(ObjectId _id) noexcept
{
  for (UniverseObject& object : objects)
  {
    if (object.id == _id)
    {
      return &object;
    }
  }
  return nullptr;
}

const ShipMotionState* UniverseSnapshot::FindMotion(ObjectId _id) const noexcept
{
  for (const ShipMotionState& motion : motions)
  {
    if (motion.object == _id)
    {
      return &motion;
    }
  }
  return nullptr;
}

} // namespace Outpost
