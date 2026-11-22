#pragma once

#include "DockingPoint.h"
#include "FlightModel.h"
#include "UniverseObject.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

// The universe as plain data. The server keeps its state in one; the client builds its own from the
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
  ObjectId object = NO_OBJECT; // the universe object this ship is

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

/// What a flying ship's engine is doing as of a tick, for presentation: plumes and their glow. The
/// controls are those the manoeuvre layer settled on, after its arbiter.
struct ShipMotionState
{
  ObjectId object = NO_OBJECT;
  std::uint64_t tick = 0;
  float throttle = 0.0f;
  float speedMetresPerSecond = 0.0f;
  bool thruster = false;
  bool engineOn = true;
  CruiseState cruise = CruiseState::Off;

  friend bool operator==(const ShipMotionState&, const ShipMotionState&) = default;
};

/// Where the client's own ship is in docking and launching, as the server last stated it.
struct DockStatusState
{
  std::uint64_t tick = 0; // the tick the server stated it
  DockPhase phase = DockPhase::None;
  DockAnswer answer = DockAnswer::None; // the answer this statement carries, or None for a change of phase
  std::string station;                  // the station docked at or asked, by name (R24)
  std::string reason;                   // why a launch was refused

  friend bool operator==(const DockStatusState&, const DockStatusState&) = default;
};

struct UniverseSnapshot
{
  std::uint64_t tick = 0;
  std::vector<UniverseObject> objects; // in the order the server created them
  std::vector<ShipState> ships;
  std::vector<ObjectiveState> objectives;
  std::vector<ResourceState> resources;
  std::vector<std::uint64_t> resourceTicks; // per resource, the tick of the copy held (newest wins)
  std::vector<ShipMotionState> motions;     // one per flying ship, newest wins
  // Raised whenever an object appears, so that presentation rebuilds its instances only then.
  std::uint64_t objectsRevision = 0;
  // Raised whenever an object moves.
  std::uint64_t posesRevision = 0;

  // The ship this client flies, as the server last stated it (OwnShipMessage). The server's own
  // snapshot leaves these empty: which ship a session flies is the session's, not the universe's.
  ObjectId ownShip = NO_OBJECT;
  std::uint64_t ownShipTick = 0;
  FlightState ownShipState;
  std::uint64_t ownShipRevision = 0;
  // Its docking, as the server last stated it (DockStatusMessage); raised with every statement.
  DockStatusState ownDock;
  std::uint64_t ownDockRevision = 0;

  [[nodiscard]] const ShipState* FindShip(std::string_view _instance) const noexcept;
  [[nodiscard]] const UniverseObject* FindObject(ObjectId _id) const noexcept;
  [[nodiscard]] UniverseObject* FindObject(ObjectId _id) noexcept;
  [[nodiscard]] const ShipMotionState* FindMotion(ObjectId _id) const noexcept;
};

} // namespace Outpost
