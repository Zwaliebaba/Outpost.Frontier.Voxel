#pragma once

#include "WorldObject.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

// The world as plain data. The server keeps its state in one; the client builds its own from the
// messages the server's observer sends (plan task F4, ADR-008), and never sees the server's.

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
  ObjectId object = NO_OBJECT; // the world object this ship is

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
  std::vector<WorldObject> objects; // in the order the server created them
  std::vector<ShipState> ships;
  std::vector<ObjectiveState> objectives;
  std::vector<ResourceState> resources;
  std::vector<std::uint64_t> resourceTicks; // per resource, the tick of the copy held (newest wins)
  // Raised whenever an object appears or moves, so that presentation rebuilds only then.
  std::uint64_t objectsRevision = 0;

  [[nodiscard]] const ShipState* FindShip(std::string_view _instance) const noexcept;
  [[nodiscard]] const WorldObject* FindObject(ObjectId _id) const noexcept;
  [[nodiscard]] WorldObject* FindObject(ObjectId _id) noexcept;
};

} // namespace Outpost
