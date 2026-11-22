#include "SpaceSupport.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Outpost
{

namespace
{

[[nodiscard]] double DistanceSquared(const WorldPosition& _a, const WorldPosition& _b) noexcept
{
  const double x = _a.x - _b.x;
  const double y = _a.y - _b.y;
  const double z = _a.z - _b.z;
  return x * x + y * y + z * z;
}

} // namespace

std::size_t SpaceSupport::AddStation(std::string _name, std::vector<Point> _points, bool _atRest)
{
  m_stations.push_back(Station{std::move(_name), std::move(_points), _atRest});
  return m_stations.size() - 1;
}

void SpaceSupport::Clear() noexcept
{
  m_stations.clear();
  m_holds.clear();
  m_waiting.clear();
}

std::size_t SpaceSupport::FindStation(std::string_view _name) const noexcept
{
  for (std::size_t i = 0; i < m_stations.size(); ++i)
  {
    if (m_stations[i].name == _name)
    {
      return i;
    }
  }
  return NO_POINT;
}

bool SpaceSupport::Offers(std::size_t _station, DockKind _class) const noexcept
{
  return std::ranges::any_of(m_stations[_station].points, [_class](const Point& _point) { return _point.kind == _class; });
}

bool SpaceSupport::Held(std::size_t _station, std::size_t _point) const noexcept
{
  return std::ranges::any_of(m_holds,
                             [&](const Hold& _hold) { return _hold.clearance.station == _station && _hold.clearance.point == _point; });
}

SpaceSupport::Answer SpaceSupport::Check(DockKind _class, const WorldPosition& _position, std::size_t _station) const noexcept
{
  const Station& station = m_stations[_station];
  // Checks 1 to 3 need missions, locks and factions, which the MVP does not have.
  if (station.points.empty())
  {
    return Answer{DockAnswer::NoDock, NO_POINT}; // 4: the target has docking points
  }
  if (!Offers(_station, _class))
  {
    return Answer{DockAnswer::CannotDock, NO_POINT}; // 5: a point accepts the ship's class
  }
  std::size_t nearest = NO_POINT;
  double nearestDistance = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < station.points.size(); ++i)
  {
    const Point& point = station.points[i];
    if (point.kind != _class || Held(_station, i))
    {
      continue;
    }
    const double distance = DistanceSquared(point.path.entry, _position);
    if (distance < nearestDistance)
    {
      nearest = i;
      nearestDistance = distance;
    }
  }
  if (nearest == NO_POINT)
  {
    return Answer{DockAnswer::Wait, NO_POINT}; // 6: a fitting point is free
  }
  if (!station.atRest)
  {
    return Answer{DockAnswer::Wait, NO_POINT}; // 7: the target is at rest
  }
  return Answer{DockAnswer::Proceed, nearest};
}

SpaceSupport::Answer SpaceSupport::Request(ObjectId _ship, DockKind _class, const WorldPosition& _position, std::size_t _station,
                                           bool _player, std::uint64_t _tick)
{
  if (const Clearance* held = HeldBy(_ship))
  {
    return Answer{DockAnswer::Proceed, held->point};
  }
  if (Waiting(_ship))
  {
    return Answer{DockAnswer::Wait, NO_POINT};
  }
  const Answer answer = Check(_class, _position, _station);
  if (answer.answer == DockAnswer::Proceed)
  {
    m_holds.push_back(Hold{Clearance{_ship, _station, answer.point}, _tick, false});
  }
  else if (answer.answer == DockAnswer::Wait)
  {
    const Waiter waiter{_ship, _class, _position, _station, _player};
    // Players go ahead of every ship that is not a player's, behind the players already waiting.
    const auto behind = _player ? std::ranges::find_if(m_waiting, [](const Waiter& _waiter) { return !_waiter.player; }) : m_waiting.end();
    m_waiting.insert(behind, waiter);
  }
  return answer;
}

void SpaceSupport::Docked(ObjectId _ship)
{
  for (Hold& hold : m_holds)
  {
    if (hold.clearance.ship == _ship)
    {
      hold.docked = true;
    }
  }
}

void SpaceSupport::Occupy(ObjectId _ship, std::size_t _station, std::size_t _point)
{
  Release(_ship);
  m_holds.push_back(Hold{Clearance{_ship, _station, _point}, 0, true});
}

void SpaceSupport::Release(ObjectId _ship)
{
  std::erase_if(m_holds, [_ship](const Hold& _hold) { return _hold.clearance.ship == _ship; });
  std::erase_if(m_waiting, [_ship](const Waiter& _waiter) { return _waiter.ship == _ship; });
}

SpaceSupport::Heartbeat SpaceSupport::Beat(std::uint64_t _tick)
{
  Heartbeat result;
  std::erase_if(m_holds,
                [&](const Hold& _hold)
                {
                  const bool late = !_hold.docked && _tick - _hold.clearedTick >= ABANDON_TICKS;
                  if (late)
                  {
                    result.abandoned.push_back(_hold.clearance.ship);
                  }
                  return late;
                });
  for (auto waiter = m_waiting.begin(); waiter != m_waiting.end();)
  {
    const Answer answer = Check(waiter->dockingClass, waiter->position, waiter->station);
    if (answer.answer == DockAnswer::Proceed)
    {
      m_holds.push_back(Hold{Clearance{waiter->ship, waiter->station, answer.point}, _tick, false});
      result.cleared.push_back(Clearance{waiter->ship, waiter->station, answer.point});
      waiter = m_waiting.erase(waiter);
    }
    else
    {
      ++waiter;
    }
  }
  return result;
}

const SpaceSupport::Clearance* SpaceSupport::HeldBy(ObjectId _ship) const noexcept
{
  const auto found = std::ranges::find_if(m_holds, [_ship](const Hold& _hold) { return _hold.clearance.ship == _ship; });
  return found != m_holds.end() ? &found->clearance : nullptr;
}

bool SpaceSupport::Waiting(ObjectId _ship) const noexcept
{
  return std::ranges::any_of(m_waiting, [_ship](const Waiter& _waiter) { return _waiter.ship == _ship; });
}

} // namespace Outpost
