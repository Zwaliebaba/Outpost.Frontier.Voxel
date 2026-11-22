#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

// The state the server hands the client. Today both live in one process and the snapshot is
// passed by reference; when they split, this is what travels.

struct ShipModuleState
{
  std::string name;
  int healthPercent = 100;

  friend bool operator==(const ShipModuleState&, const ShipModuleState&) = default;
};

struct ShipState
{
  std::string instance; // the scene instance that draws this ship
  std::string callsign;
  std::string shipClass;
  int hullPercent = 100;
  std::vector<ShipModuleState> modules;

  /// Modules below full health, worst first.
  [[nodiscard]] std::vector<ShipModuleState> DamagedModules() const;

  friend bool operator==(const ShipState&, const ShipState&) = default;
};

struct ObjectiveState
{
  std::string text;
  bool complete = false;

  friend bool operator==(const ObjectiveState&, const ObjectiveState&) = default;
};

struct ResourceState
{
  std::string name;
  std::int64_t amount = 0;
  std::int64_t capacity = 0; // 0: uncapped; otherwise shown as amount / capacity

  friend bool operator==(const ResourceState&, const ResourceState&) = default;
};

struct WorldSnapshot
{
  std::uint64_t tick = 0;
  std::vector<ShipState> ships;
  std::vector<ObjectiveState> objectives;
  std::vector<ResourceState> resources;

  [[nodiscard]] const ShipState* FindShip(std::string_view _instance) const noexcept;
};

} // namespace Outpost
