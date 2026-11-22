#pragma once

#include "DockingPoint.h"
#include "WorldObject.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Outpost
{

/// Space support (plan task 2.3, blueprint ch. 04 §3.2): the authority's controller that answers dock
/// requests, keeps the docking list of ships cleared to a point and the waiting list of ships told to
/// wait, and frees a point when its ship has docked and launched again. The checks that apply without
/// factions or missions run in the chapter's order, and the first failure decides.
///
/// A docked ship keeps its point (GameDesign.md D16): it is held from clearance until the ship has
/// launched, so a second ship is told to wait rather than sent into an occupied berth.
class SpaceSupport
{
public:
  /// A ship not docked within this long of being cleared is thrown out (ch. 04 §3.2).
  static constexpr std::uint64_t ABANDON_TICKS = std::uint64_t{90} * 60;
  /// The queue is walked once a second (ch. 04 §3.2's heartbeat).
  static constexpr std::uint64_t HEARTBEAT_TICKS = 60;
  static constexpr std::size_t NO_POINT = ~std::size_t{0};

  struct Point
  {
    std::string instance; // the scene instance that carries it
    std::string name;     // the docking point on that instance's model
    DockKind kind = DockKind::Berth;
    DockingPath path;
  };

  struct Answer
  {
    DockAnswer answer = DockAnswer::NoDock;
    std::size_t point = NO_POINT; // with Proceed: the point, an index into the station's points
  };

  struct Clearance
  {
    ObjectId ship = NO_OBJECT;
    std::size_t station = 0;
    std::size_t point = NO_POINT;
  };

  /// What a heartbeat decided: ships thrown out for taking too long, and waiting ships now cleared.
  struct Heartbeat
  {
    std::vector<ObjectId> abandoned;
    std::vector<Clearance> cleared;
  };

  /// Adds a station and returns its index. A station at rest stays at rest: stations do not move.
  std::size_t AddStation(std::string _name, std::vector<Point> _points, bool _atRest = true);
  void Clear() noexcept;

  [[nodiscard]] std::size_t StationCount() const noexcept
  {
    return m_stations.size();
  }
  [[nodiscard]] const std::string& StationName(std::size_t _station) const noexcept
  {
    return m_stations[_station].name;
  }
  [[nodiscard]] const std::vector<Point>& Points(std::size_t _station) const noexcept
  {
    return m_stations[_station].points;
  }
  [[nodiscard]] std::size_t FindStation(std::string_view _name) const noexcept;
  [[nodiscard]] bool Offers(std::size_t _station, DockKind _class) const noexcept;

  /// A ship asks to dock (ch. 04 §3.2). Proceed puts it on the docking list with the free fitting
  /// point nearest it; Wait puts it on the waiting list, a player ahead of every ship that is not
  /// one and behind the players already waiting. A ship already on either list is answered from it.
  [[nodiscard]] Answer Request(ObjectId _ship, DockKind _class, const WorldPosition& _position, std::size_t _station, bool _player,
                               std::uint64_t _tick);

  /// The cleared ship reached its point: it now holds it as a docked ship, until Release.
  void Docked(ObjectId _ship);
  /// A ship placed docked without asking, by a new game or a save: it holds the point from now.
  void Occupy(ObjectId _ship, std::size_t _station, std::size_t _point);
  /// The ship left its point, by launching, cancelling or being thrown out, or left the waiting list.
  void Release(ObjectId _ship);

  /// Throws out cleared ships that have not docked within ABANDON_TICKS, then walks the waiting list
  /// from the front, each waiter passing the whole check again. Call it every HEARTBEAT_TICKS and
  /// after a ship has docked or left.
  [[nodiscard]] Heartbeat Beat(std::uint64_t _tick);

  /// The point a ship holds, cleared or docked, or null.
  [[nodiscard]] const Clearance* HeldBy(ObjectId _ship) const noexcept;
  [[nodiscard]] bool Waiting(ObjectId _ship) const noexcept;
  [[nodiscard]] std::size_t WaitingCount() const noexcept
  {
    return m_waiting.size();
  }

private:
  struct Station
  {
    std::string name;
    std::vector<Point> points;
    bool atRest = true;
  };

  struct Hold
  {
    Clearance clearance;
    std::uint64_t clearedTick = 0;
    bool docked = false;
  };

  struct Waiter
  {
    ObjectId ship = NO_OBJECT;
    DockKind dockingClass = DockKind::Berth;
    WorldPosition position;
    std::size_t station = 0;
    bool player = false;
  };

  [[nodiscard]] Answer Check(DockKind _class, const WorldPosition& _position, std::size_t _station) const noexcept;
  [[nodiscard]] bool Held(std::size_t _station, std::size_t _point) const noexcept;

  std::vector<Station> m_stations;
  std::vector<Hold> m_holds;     // the docking list, docked ships included
  std::vector<Waiter> m_waiting; // in the order they will be cleared
};

} // namespace Outpost
