#include "WorldSnapshot.h"

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

const ShipState* WorldSnapshot::FindShip(std::string_view _instance) const noexcept
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

} // namespace Outpost
