#include "World.h"

#include "TickClock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

using namespace DirectX;

namespace Outpost
{

namespace
{

constexpr std::uint32_t TICKS_PER_SECOND = Neuron::TickClock::TICKS_PER_SECOND;

[[nodiscard]] double Distance(const WorldPosition& _a, const WorldPosition& _b) noexcept
{
  const double x = _a.x - _b.x;
  const double y = _a.y - _b.y;
  const double z = _a.z - _b.z;
  return std::sqrt(x * x + y * y + z * z);
}

/// A share of the way along a polyline of three points.
[[nodiscard]] WorldPosition Along(const std::array<WorldPosition, 3>& _points, double _metres) noexcept
{
  for (std::size_t i = 0; i + 1 < _points.size(); ++i)
  {
    const double length = Distance(_points[i], _points[i + 1]);
    if (_metres <= length || i + 2 == _points.size())
    {
      const double share = length > 0.0 ? std::clamp(_metres / length, 0.0, 1.0) : 1.0;
      const WorldPosition& a = _points[i];
      const WorldPosition& b = _points[i + 1];
      return WorldPosition{a.x + (b.x - a.x) * share, a.y + (b.y - a.y) * share, a.z + (b.z - a.z) * share};
    }
    _metres -= length;
  }
  return _points.back();
}

/// The ship's box where its pose puts it: the box's offset from the pivot turns with the ship.
[[nodiscard]] CollisionBox PlaceBox(const CollisionBox& _local, const FlightState& _state) noexcept
{
  CollisionBox box = _local;
  XMFLOAT3 offset{};
  XMStoreFloat3(&offset, XMVector3Rotate(XMVectorSet(static_cast<float>(_local.center.x), static_cast<float>(_local.center.y),
                                                     static_cast<float>(_local.center.z), 0.0f),
                                         XMLoadFloat4(&_state.orientation)));
  box.center = WorldPosition{_state.position.x + static_cast<double>(offset.x), _state.position.y + static_cast<double>(offset.y),
                             _state.position.z + static_cast<double>(offset.z)};
  box.orientation = _state.orientation;
  box.velocityMetresPerSecond = _state.velocityMetresPerSecond;
  return box;
}

} // namespace

bool World::LoadContent(const std::filesystem::path& _assetRoot, std::string& _outError)
{
  SceneDefinition scene;
  if (!LoadSceneContent(_assetRoot, scene, _outError))
  {
    return false;
  }
  ShipContent ships;
  if (!LoadShipContent(_assetRoot / "Content" / "Ships.txt", ships, _outError))
  {
    return false;
  }
  const std::vector<std::string> problems = ResolveShipModels(ships, scene);
  if (!problems.empty())
  {
    _outError = problems.front();
    return false;
  }
  SystemContent system;
  if (!LoadSystemContent(_assetRoot / "Content" / "System.txt", scene, system, _outError))
  {
    return false;
  }
  Load(scene, ships, system);
  if (!scene.playerShip.empty() && m_playerShip == NO_OBJECT)
  {
    _outError = "player ship '" + scene.playerShip + "' has no ship archetype to fly with";
    return false;
  }
  return true;
}

void World::Load(const SceneDefinition& _scene, const ShipContent& _ships, const SystemContent& _system)
{
  m_state = WorldSnapshot{};
  m_system = _system;
  m_character = Character{};
  m_saveRequested = false;
  m_support.Clear();
  for (const StationDefinition& station : _system.stations)
  {
    std::vector<SpaceSupport::Point> points;
    for (const std::string& instanceName : station.instances)
    {
      const int index = _scene.InstanceIndex(instanceName);
      const ModelAsset* model = index >= 0 ? _scene.FindModel(_scene.instances[static_cast<std::size_t>(index)].model) : nullptr;
      if (model == nullptr)
      {
        continue;
      }
      for (const DockingPoint& point : model->dockingPoints)
      {
        points.push_back(
          SpaceSupport::Point{instanceName, point.name, point.kind, PathFor(_scene, static_cast<std::size_t>(index), point)});
      }
    }
    static_cast<void>(m_support.AddStation(station.name, std::move(points)));
  }
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

  m_staticBodies = Outpost::StaticBodies(_scene, _ships, m_state.objects);
  m_flyers.clear();
  m_playerShip = NO_OBJECT;
  m_commands.clear();
  m_heldCommand = ShipCommand{};
  m_commandsStarted = false;
  m_contacts.clear();
  m_lastContacts.clear();
  const int playerIndex = _scene.playerShip.empty() ? -1 : _scene.InstanceIndex(_scene.playerShip);
  for (std::size_t i = 0; i < m_state.objects.size(); ++i)
  {
    const WorldObject& object = m_state.objects[i];
    const ShipArchetype* archetype = FlyingArchetype(_ships, object);
    if (archetype == nullptr)
    {
      continue;
    }
    Flyer flyer;
    flyer.objectIndex = i;
    flyer.body = ShipBodyFor(_scene, _ships, *archetype, object, &flyer.localBox);
    flyer.massKilograms = archetype->massKilograms;
    flyer.dockingClass = archetype->dockingClass;
    flyer.state = RestingFlightState(flyer.body.spec, object.pose);
    if (static_cast<int>(i) == playerIndex)
    {
      m_playerShip = object.id;
      flyer.manoeuvre = Manoeuvre::FreeFlight();
    }
    else
    {
      flyer.manoeuvre = Manoeuvre::Hold(object.pose.position);
    }
    m_flyers.push_back(flyer);
    m_state.motions.push_back(ShipMotionState{object.id, 0, 0.0f, 0.0f, false, true, CruiseState::Off});
  }
}

void World::Tick()
{
  ++m_state.tick;
  PayIncome();
  FlyShips();
  if (m_state.tick % SpaceSupport::HEARTBEAT_TICKS == 0)
  {
    BeatSupport();
  }
}

void World::PayIncome()
{
  // Integer income: perMinute is spread over the minute's ticks with the remainder carried, so
  // the total after a minute is exact and the result never depends on frame timing.
  constexpr std::int64_t TICKS_PER_MINUTE = std::int64_t{TICKS_PER_SECOND} * 60;
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

void World::FlyShips()
{
  const std::uint64_t tick = m_state.tick;
  // Every body: the immovable ones, then each ship where it stands, except a ship that is carried or
  // docked, which nothing meets (D16). A ship that has moved this tick is met where it went; one that
  // has not yet, where it was.
  std::vector<CollisionBox> bodies = m_staticBodies;
  std::vector<std::size_t> bodyOf(m_flyers.size(), SpaceSupport::NO_POINT);
  const auto carried = [](const Flyer& _flyer)
  { return _flyer.dock.phase == DockPhase::Final || _flyer.dock.phase == DockPhase::Docked || _flyer.dock.phase == DockPhase::Launching; };
  for (std::size_t i = 0; i < m_flyers.size(); ++i)
  {
    if (!carried(m_flyers[i]))
    {
      bodyOf[i] = bodies.size();
      bodies.push_back(PlaceBox(m_flyers[i].localBox, m_flyers[i].state));
    }
  }

  for (std::size_t i = 0; i < m_flyers.size(); ++i)
  {
    Flyer& flyer = m_flyers[i];
    const bool player = flyer.body.object == m_playerShip;
    if (player)
    {
      // Commands are in tick order and none is older than this tick, so this tick's is the first.
      const bool commanded = !m_commands.empty() && m_commands.front().first == tick;
      if (flyer.dock.phase != DockPhase::None)
      {
        // The server flies the ship: the player's commands are set aside, and none is missed.
      }
      else if (commanded)
      {
        m_heldCommand = m_commands.front().second;
      }
      else if (m_commandsStarted)
      {
        flyer.divergedAt = tick; // the client predicted this tick on a command the server never saw
      }
      const auto later = std::ranges::find_if(m_commands, [tick](const auto& _entry) { return _entry.first > tick; });
      m_commands.erase(m_commands.begin(), later);
    }

    ShipCommand command{flyer.manoeuvre, FlightControls{}};
    switch (flyer.dock.phase)
    {
    case DockPhase::None:
      if (player)
      {
        command = m_heldCommand;
      }
      break;
    case DockPhase::FarApproach:
      command.manoeuvre = Manoeuvre::GoTo(flyer.farTarget, FAR_APPROACH_METRES * 0.9f);
      break;
    case DockPhase::Waiting:
      command.manoeuvre = Manoeuvre::Hold(flyer.holdAnchor);
      break;
    case DockPhase::Staging:
      command.manoeuvre = Manoeuvre::GoTo(flyer.axisPoint.value_or(m_support.Points(flyer.dockStation)[flyer.dockPoint].path.entry), 0.0f);
      break;
    case DockPhase::Final:
    case DockPhase::Docked:
    case DockPhase::Launching:
      break;
    }

    ShipTickResult result;
    if (carried(flyer))
    {
      if (flyer.dock.phase != DockPhase::Docked && StepMover(flyer))
      {
        if (flyer.dock.phase == DockPhase::Final)
        {
          // Docked: within the point's radius of the pad, which the mover ends on (ch. 04 §3.4).
          m_support.Docked(flyer.body.object);
          flyer.state.velocityMetresPerSecond = XMFLOAT3{};
          flyer.state.angularVelocityRadiansPerSecond = XMFLOAT3{};
          StateDock(flyer, DockPhase::Docked, DockAnswer::None);
          if (player)
          {
            m_character.baseSnapshot = ShipRecordOf(flyer.body.object);
            m_saveRequested = true; // D17: saved on docking
          }
          BeatSupport();
        }
        else
        {
          Release(flyer);
        }
      }
      result.controls.engineOn = flyer.dock.phase != DockPhase::Docked;
      result.controls.throttle = flyer.dock.phase == DockPhase::Docked ? 0.0f : 0.2f;
    }
    else
    {
      result = StepShip(flyer.body, command, bodies, TICKS_PER_SECOND, flyer.state);
      flyer.arrived = result.arrived;
      if (result.contact)
      {
        const auto body = std::ranges::find(bodies, result.contactObject, &CollisionBox::object);
        RecordContact(flyer, result, body != bodies.end() ? body->massKilograms : 0.0f);
        if (result.contactMoves && flyer.dock.phase == DockPhase::None)
        {
          flyer.divergedAt = tick; // the prediction does not know where other ships are
        }
      }
      bodies[bodyOf[i]] = PlaceBox(flyer.localBox, flyer.state);
      if (flyer.dock.phase == DockPhase::FarApproach)
      {
        const double distance = Distance(flyer.state.position, flyer.farTarget);
        if (distance <= static_cast<double>(FAR_APPROACH_METRES))
        {
          AskForPoint(flyer);
        }
      }
      else if (flyer.dock.phase == DockPhase::Staging && result.arrived && flyer.axisPoint.has_value())
      {
        flyer.axisPoint.reset(); // on the axis: in along it to the entry
      }
      else if (flyer.dock.phase == DockPhase::Staging && result.arrived)
      {
        const DockingPath& path = m_support.Points(flyer.dockStation)[flyer.dockPoint].path;
        StartMover(flyer, flyer.state.position, path.approach, path.pad, path.orientation, false);
        StateDock(flyer, DockPhase::Final, DockAnswer::None);
      }
    }

    SetPose(flyer);
    ShipMotionState motion{flyer.body.object,
                           tick,
                           result.controls.engineOn ? result.controls.throttle : 0.0f,
                           flyer.state.SpeedMetresPerSecond(),
                           flyer.state.thrusterActive,
                           result.controls.engineOn,
                           flyer.state.cruise};
    m_state.motions[i] = motion;
  }
}

void World::SetPose(Flyer& _flyer)
{
  WorldObject& object = m_state.objects[_flyer.objectIndex];
  const Pose pose{_flyer.state.position, _flyer.state.orientation};
  if (!(object.pose == pose))
  {
    object.pose = pose;
    object.poseTick = m_state.tick;
    ++m_state.posesRevision;
  }
}

void World::RecordContact(const Flyer& _flyer, const ShipTickResult& _result, float _bodyMass)
{
  const std::uint64_t tick = m_state.tick;
  const auto last = std::ranges::find_if(m_lastContacts, [&](const LastContact& _contact)
                                         { return _contact.ship == _flyer.body.object && _contact.body == _result.contactObject; });
  const auto repeatTicks = static_cast<std::uint64_t>(std::lround(CONTACT_REPEAT_SECONDS * static_cast<float>(TICKS_PER_SECOND)));
  if (last != m_lastContacts.end() && tick - last->tick < repeatTicks)
  {
    return;
  }
  if (last != m_lastContacts.end())
  {
    last->tick = tick;
  }
  else
  {
    m_lastContacts.push_back(LastContact{_flyer.body.object, _result.contactObject, tick});
  }
  const float speed = _result.contactSpeedMetresPerSecond;
  m_contacts.push_back(ContactRecord{tick, _flyer.body.object, _result.contactObject, speed,
                                     CollisionDamage(speed, _bodyMass, _flyer.massKilograms),
                                     CollisionDamage(speed, _flyer.massKilograms, _bodyMass)});
  if (m_contacts.size() > MAX_CONTACT_RECORDS)
  {
    m_contacts.erase(m_contacts.begin());
  }
}

bool World::SubmitCommand(const FlightInputMessage& _input)
{
  if (_input.ship == NO_OBJECT || _input.ship != m_playerShip || _input.tick <= m_state.tick ||
      _input.tick > m_state.tick + MAX_COMMAND_LEAD_TICKS)
  {
    return false;
  }
  // A command resent for the same tick replaces the first; otherwise it is inserted in tick order.
  const auto at = std::ranges::lower_bound(m_commands, _input.tick, {}, [](const auto& _entry) { return _entry.first; });
  if (at != m_commands.end() && at->first == _input.tick)
  {
    at->second = _input.command;
  }
  else
  {
    m_commands.insert(at, std::make_pair(_input.tick, _input.command));
  }
  m_commandsStarted = true;
  return true;
}

bool World::Direct(ObjectId _ship, const Manoeuvre& _manoeuvre)
{
  Flyer* flyer = FindFlyer(_ship);
  if (flyer == nullptr || _ship == m_playerShip)
  {
    return false;
  }
  flyer->manoeuvre = _manoeuvre;
  return true;
}

const FlightState* World::Flight(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr ? &flyer->state : nullptr;
}

std::uint64_t World::DivergedAt(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr ? flyer->divergedAt : 0;
}

bool World::Arrived(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr && flyer->arrived;
}

void World::BeatSupport()
{
  const SpaceSupport::Heartbeat beat = m_support.Beat(m_state.tick);
  for (const ObjectId ship : beat.abandoned)
  {
    if (Flyer* flyer = FindFlyer(ship))
    {
      flyer->dockStation = SpaceSupport::NO_POINT;
      flyer->dockPoint = SpaceSupport::NO_POINT;
      flyer->manoeuvre = Manoeuvre::Hold(flyer->state.position);
      StateDock(*flyer, DockPhase::None, DockAnswer::TimedOut);
    }
  }
  for (const SpaceSupport::Clearance& clearance : beat.cleared)
  {
    if (Flyer* flyer = FindFlyer(clearance.ship))
    {
      Clear(*flyer, clearance.point);
    }
  }
}

void World::StateDock(Flyer& _flyer, DockPhase _phase, DockAnswer _answer, std::string _reason)
{
  const bool freed = _flyer.dock.phase != DockPhase::None && _phase == DockPhase::None;
  _flyer.dock.tick = m_state.tick;
  _flyer.dock.phase = _phase;
  _flyer.dock.answer = _answer;
  _flyer.dock.reason = std::move(_reason);
  _flyer.dock.station = _flyer.dockStation != SpaceSupport::NO_POINT ? m_support.StationName(_flyer.dockStation) : _flyer.dock.station;
  if (freed && _flyer.body.object == m_playerShip)
  {
    // The player flies again: the client is told the whole state and predicts on from it, and the
    // ticks before its first command are not counted as missed.
    _flyer.divergedAt = m_state.tick;
    m_heldCommand = ShipCommand{};
    m_commandsStarted = false;
  }
}

void World::AskForPoint(Flyer& _flyer)
{
  const SpaceSupport::Answer answer = m_support.Request(_flyer.body.object, _flyer.dockingClass, _flyer.state.position, _flyer.dockStation,
                                                        _flyer.body.object == m_playerShip, m_state.tick);
  switch (answer.answer)
  {
  case DockAnswer::Proceed:
    Clear(_flyer, answer.point);
    break;
  case DockAnswer::Wait:
    _flyer.holdAnchor = _flyer.state.position;
    StateDock(_flyer, DockPhase::Waiting, DockAnswer::Wait);
    break;
  default:
    _flyer.dockStation = SpaceSupport::NO_POINT;
    StateDock(_flyer, DockPhase::None, answer.answer);
    break;
  }
}

void World::Clear(Flyer& _flyer, std::size_t _point)
{
  _flyer.dockPoint = _point;
  // Straight to the entry when nothing immovable is in the way; otherwise by the docking axis.
  const DockingPath& path = m_support.Points(_flyer.dockStation)[_point].path;
  const WorldPosition& from = _flyer.state.position;
  const XMFLOAT3 toEntry{static_cast<float>(path.entry.x - from.x), static_cast<float>(path.entry.y - from.y),
                         static_cast<float>(path.entry.z - from.z)};
  SweepHit hit;
  _flyer.axisPoint.reset();
  if (SweepSphere(from, toEntry, _flyer.body.radiusMetres, m_staticBodies, _flyer.body.object, hit))
  {
    const double length = Distance(path.entry, path.approach);
    const double share = length > 0.0 ? static_cast<double>(AXIS_STANDOFF_METRES) / length : 0.0;
    _flyer.axisPoint =
      WorldPosition{path.entry.x + (path.entry.x - path.approach.x) * share, path.entry.y + (path.entry.y - path.approach.y) * share,
                    path.entry.z + (path.entry.z - path.approach.z) * share};
  }
  StateDock(_flyer, DockPhase::Staging, DockAnswer::Proceed);
}

void World::StartMover(Flyer& _flyer, const WorldPosition& _from, const WorldPosition& _via, const WorldPosition& _to,
                       const XMFLOAT4& _toOrientation, bool _outward)
{
  Mover& mover = _flyer.mover;
  mover.points = {_from, _via, _to};
  mover.lengthMetres = Distance(_from, _via) + Distance(_via, _to);
  // Eased in and out, the approach peaks at 1.5 times its mean speed; a launch from rest leaves at
  // twice its mean speed.
  const double meanSpeed = _outward ? LAUNCH_RELEASE_METRES_PER_SECOND / 2.0 : DOCKING_PEAK_METRES_PER_SECOND / 1.5;
  mover.durationTicks =
    std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil(mover.lengthMetres / meanSpeed * TICKS_PER_SECOND)));
  mover.startTick = m_state.tick;
  mover.fromOrientation = _flyer.state.orientation;
  mover.toOrientation = _toOrientation;
  mover.outward = _outward;
  _flyer.state.cruise = CruiseState::Off;
  _flyer.state.cruiseTimerTicks = 0;
  _flyer.state.cruiseRamp = 0.0f;
  _flyer.state.thrusterActive = false;
}

bool World::StepMover(Flyer& _flyer)
{
  const Mover& mover = _flyer.mover;
  const double share = std::min(1.0, static_cast<double>(m_state.tick - mover.startTick) / static_cast<double>(mover.durationTicks));
  const double travelled = mover.outward ? share * share : share * share * (3.0 - 2.0 * share);
  const WorldPosition before = _flyer.state.position;
  _flyer.state.position = Along(mover.points, travelled * mover.lengthMetres);
  _flyer.state.velocityMetresPerSecond = XMFLOAT3{static_cast<float>((_flyer.state.position.x - before.x) * TICKS_PER_SECOND),
                                                  static_cast<float>((_flyer.state.position.y - before.y) * TICKS_PER_SECOND),
                                                  static_cast<float>((_flyer.state.position.z - before.z) * TICKS_PER_SECOND)};
  // The approach turns the ship to the pad's heading over its first two fifths; a launch keeps it.
  const float turn = mover.outward ? 1.0f : static_cast<float>(std::min(1.0, share / 0.4));
  XMStoreFloat4(&_flyer.state.orientation,
                XMQuaternionNormalize(XMQuaternionSlerp(XMLoadFloat4(&mover.fromOrientation), XMLoadFloat4(&mover.toOrientation), turn)));
  _flyer.state.angularVelocityRadiansPerSecond = XMFLOAT3{};
  return share >= 1.0;
}

void World::PlaceDocked(Flyer& _flyer, std::size_t _station, std::size_t _point)
{
  const SpaceSupport::Point& point = m_support.Points(_station)[_point];
  m_support.Occupy(_flyer.body.object, _station, _point);
  _flyer.dockStation = _station;
  _flyer.dockPoint = _point;
  _flyer.state = RestingFlightState(_flyer.body.spec, Pose{point.path.pad, point.path.orientation});
  _flyer.manoeuvre = Manoeuvre::FreeFlight();
  _flyer.dock = DockStatusState{};
  StateDock(_flyer, DockPhase::Docked, DockAnswer::None);
  SetPose(_flyer);
}

void World::Release(Flyer& _flyer)
{
  // Released at the entry, moving outward at the mover's final speed, with collisions on again.
  const SpaceSupport::Point& point = m_support.Points(_flyer.dockStation)[_flyer.dockPoint];
  const DockingPath& path = point.path;
  const XMVECTOR out =
    XMVector3Normalize(XMVectorSet(static_cast<float>(path.entry.x - path.approach.x), static_cast<float>(path.entry.y - path.approach.y),
                                   static_cast<float>(path.entry.z - path.approach.z), 0.0f));
  const ShipFlightSpec& spec = _flyer.body.spec;
  _flyer.state = RestingFlightState(spec, Pose{path.entry, path.orientation});
  XMStoreFloat3(&_flyer.state.velocityMetresPerSecond, XMVectorScale(out, LAUNCH_RELEASE_METRES_PER_SECOND));
  m_support.Release(_flyer.body.object);
  _flyer.dockPoint = SpaceSupport::NO_POINT;
  _flyer.manoeuvre = _flyer.body.object == m_playerShip ? Manoeuvre::FreeFlight() : Manoeuvre::Hold(path.entry);
  StateDock(_flyer, DockPhase::None, DockAnswer::None);
  _flyer.dockStation = SpaceSupport::NO_POINT;
  BeatSupport();
}

bool World::Command(const DockCommandMessage& _command)
{
  Flyer* flyer = FindFlyer(_command.ship);
  if (flyer == nullptr || _command.ship != m_playerShip)
  {
    return false;
  }
  switch (_command.command)
  {
  case DockCommand::Dock:
  {
    if (flyer->dock.phase != DockPhase::None)
    {
      return false;
    }
    // The nearest station in range that has a point for the ship's class (D20), measured to the
    // nearest fitting entry.
    std::size_t nearestStation = SpaceSupport::NO_POINT;
    double nearest = static_cast<double>(DOCK_RANGE_METRES);
    for (std::size_t station = 0; station < m_support.StationCount(); ++station)
    {
      for (const SpaceSupport::Point& point : m_support.Points(station))
      {
        const double distance = Distance(point.path.entry, flyer->state.position);
        if (point.kind == flyer->dockingClass && distance <= nearest)
        {
          nearest = distance;
          nearestStation = station;
          flyer->farTarget = point.path.entry;
        }
      }
    }
    if (nearestStation == SpaceSupport::NO_POINT)
    {
      StateDock(*flyer, DockPhase::None, DockAnswer::OutOfRange);
      return true;
    }
    flyer->dockStation = nearestStation;
    if (nearest > static_cast<double>(FAR_APPROACH_METRES))
    {
      StateDock(*flyer, DockPhase::FarApproach, DockAnswer::None);
    }
    else
    {
      AskForPoint(*flyer);
    }
    return true;
  }
  case DockCommand::Cancel:
  {
    const DockPhase phase = flyer->dock.phase;
    if (phase != DockPhase::FarApproach && phase != DockPhase::Waiting && phase != DockPhase::Staging)
    {
      return false; // once carried, a docking or launch runs to its end (ch. 04 §3.3)
    }
    m_support.Release(flyer->body.object);
    flyer->dockPoint = SpaceSupport::NO_POINT;
    StateDock(*flyer, DockPhase::None, DockAnswer::Canceled);
    flyer->dockStation = SpaceSupport::NO_POINT;
    BeatSupport();
    return true;
  }
  case DockCommand::Launch:
  {
    if (flyer->dock.phase != DockPhase::Docked)
    {
      return false;
    }
    // A ship that cannot fly is not launched (plan task 2.5).
    const ShipFlightSpec& spec = flyer->body.spec;
    if (!(spec.engineThrustNewtons > 0.0f))
    {
      StateDock(*flyer, DockPhase::Docked, DockAnswer::LaunchRefused, "the ship has no working engine");
      return true;
    }
    if (!(spec.powerCapacity > 0.0f))
    {
      StateDock(*flyer, DockPhase::Docked, DockAnswer::LaunchRefused, "the ship has no power plant");
      return true;
    }
    const DockingPath& path = m_support.Points(flyer->dockStation)[flyer->dockPoint].path;
    StartMover(*flyer, flyer->state.position, path.approach, path.entry, path.orientation, true);
    StateDock(*flyer, DockPhase::Launching, DockAnswer::None);
    m_saveRequested = true; // D17: saved on launch
    return true;
  }
  }
  return false;
}

DockStatusState World::DockStatus(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr ? flyer->dock : DockStatusState{};
}

bool World::Protected(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  const DockPhase phase = flyer != nullptr ? flyer->dock.phase : DockPhase::None;
  return phase == DockPhase::Final || phase == DockPhase::Docked || phase == DockPhase::Launching;
}

bool World::TakeSaveRequest() noexcept
{
  return std::exchange(m_saveRequested, false);
}

ShipState* World::ShipStateOf(ObjectId _ship) noexcept
{
  const auto found = std::ranges::find(m_state.ships, _ship, &ShipState::object);
  return found != m_state.ships.end() ? &*found : nullptr;
}

const ShipState* World::ShipStateOf(ObjectId _ship) const noexcept
{
  const auto found = std::ranges::find(m_state.ships, _ship, &ShipState::object);
  return found != m_state.ships.end() ? &*found : nullptr;
}

CharacterShip World::ShipRecordOf(ObjectId _ship) const
{
  CharacterShip record;
  if (const WorldObject* object = m_state.FindObject(_ship))
  {
    record.instance = object->name;
  }
  if (const ShipState* ship = ShipStateOf(_ship))
  {
    record.hullPercent = ship->hullPercent;
    record.modules = ship->modules;
  }
  return record;
}

bool World::NewGame(std::string& _outError)
{
  Flyer* flyer = FindFlyer(m_playerShip);
  if (flyer == nullptr)
  {
    _outError = "the scene gives the player no ship to fly";
    return false;
  }
  const std::size_t station = m_support.FindStation(m_system.startStation);
  if (station == SpaceSupport::NO_POINT)
  {
    _outError = "no start station '" + m_system.startStation + "'";
    return false;
  }
  const SpaceSupport::Answer answer =
    m_support.Request(m_playerShip, flyer->dockingClass, flyer->state.position, station, true, m_state.tick);
  if (answer.answer != DockAnswer::Proceed)
  {
    m_support.Release(m_playerShip);
    _outError = "the start station '" + m_system.startStation + "' has no free point for the player's ship (" +
                std::string(DockAnswerName(answer.answer)) + ")";
    return false;
  }
  m_character = Character{};
  m_character.ship = ShipRecordOf(m_playerShip);
  m_character.baseSnapshot = m_character.ship;
  PlaceDocked(*flyer, station, answer.point);
  return true;
}

bool World::Restore(const SaveGame& _save, std::string& _outError)
{
  Flyer* flyer = FindFlyer(m_playerShip);
  const WorldObject* object = m_state.FindObject(m_playerShip);
  if (flyer == nullptr || object == nullptr)
  {
    _outError = "the scene gives the player no ship to fly";
    return false;
  }
  const Character& character = _save.character;
  if (character.ship.instance != object->name)
  {
    _outError = "the save flies '" + character.ship.instance + "'; the scene gives the player '" + object->name + "'";
    return false;
  }
  // Everything is checked before anything changes, so a refused save leaves the world as it was.
  std::vector<std::size_t> resourceIndices;
  for (const ResourceSave& resource : _save.world.resources)
  {
    const auto found = std::ranges::find(m_state.resources, resource.name, &ResourceState::name);
    if (found == m_state.resources.end())
    {
      _outError = "the save names unknown resource '" + resource.name + "'";
      return false;
    }
    resourceIndices.push_back(static_cast<std::size_t>(found - m_state.resources.begin()));
  }
  std::vector<std::size_t> objectiveIndices;
  for (const ObjectiveSave& objective : _save.world.objectives)
  {
    const auto found = std::ranges::find(m_state.objectives, objective.text, &ObjectiveState::text);
    if (found == m_state.objectives.end())
    {
      _outError = "the save names unknown objective '" + objective.text + "'";
      return false;
    }
    objectiveIndices.push_back(static_cast<std::size_t>(found - m_state.objectives.begin()));
  }
  std::size_t station = SpaceSupport::NO_POINT;
  std::size_t point = SpaceSupport::NO_POINT;
  if (character.location.kind == LocationKind::Docked)
  {
    station = m_support.FindStation(character.location.station);
    if (station == SpaceSupport::NO_POINT)
    {
      _outError = "the save is docked at unknown station '" + character.location.station + "'";
      return false;
    }
    const std::vector<SpaceSupport::Point>& points = m_support.Points(station);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
      if (points[i].instance == character.location.instance && points[i].name == character.location.point)
      {
        point = i;
      }
    }
    if (point == SpaceSupport::NO_POINT || points[point].kind != flyer->dockingClass)
    {
      _outError = "the save is docked at '" + character.location.instance + "." + character.location.point + "', which station '" +
                  character.location.station + "' has no fitting point of";
      return false;
    }
  }

  m_state.tick = _save.world.tick;
  for (std::size_t i = 0; i < resourceIndices.size(); ++i)
  {
    m_state.resources[resourceIndices[i]].amount = _save.world.resources[i].amount;
    m_income[resourceIndices[i]].remainder = _save.world.resources[i].remainder;
    m_state.resourceTicks[resourceIndices[i]] = m_state.tick;
  }
  for (std::size_t i = 0; i < objectiveIndices.size(); ++i)
  {
    m_state.objectives[objectiveIndices[i]].complete = _save.world.objectives[i].complete;
  }
  m_character = character;
  if (ShipState* ship = ShipStateOf(m_playerShip))
  {
    ship->hullPercent = character.ship.hullPercent;
    ship->modules = character.ship.modules;
  }
  m_commands.clear();
  m_heldCommand = ShipCommand{};
  m_commandsStarted = false;
  if (station != SpaceSupport::NO_POINT)
  {
    PlaceDocked(*flyer, station, point);
  }
  else
  {
    m_support.Release(m_playerShip);
    flyer->state = character.location.flight;
    flyer->manoeuvre = Manoeuvre::FreeFlight();
    flyer->dock = DockStatusState{};
    flyer->dockStation = SpaceSupport::NO_POINT;
    flyer->dockPoint = SpaceSupport::NO_POINT;
    SetPose(*flyer);
  }
  // Every object's pose is stated as of the restored tick, so the client's newest-wins copies take it.
  for (WorldObject& each : m_state.objects)
  {
    each.poseTick = m_state.tick;
  }
  for (ShipMotionState& motion : m_state.motions)
  {
    motion.tick = m_state.tick;
  }
  return true;
}

SaveGame World::MakeSave() const
{
  SaveGame save;
  save.character = m_character;
  save.character.ship = ShipRecordOf(m_playerShip);
  CharacterLocation& location = save.character.location;
  const Flyer* flyer = FindFlyer(m_playerShip);
  const DockPhase phase = flyer != nullptr ? flyer->dock.phase : DockPhase::None;
  if (flyer != nullptr && (phase == DockPhase::Final || phase == DockPhase::Docked || phase == DockPhase::Launching))
  {
    // A ship carried in or out is saved in the berth it is carried to or from.
    const SpaceSupport::Point& point = m_support.Points(flyer->dockStation)[flyer->dockPoint];
    location.kind = LocationKind::Docked;
    location.station = m_support.StationName(flyer->dockStation);
    location.instance = point.instance;
    location.point = point.name;
  }
  else
  {
    location = CharacterLocation{};
    location.kind = LocationKind::Space;
    if (flyer != nullptr)
    {
      location.flight = flyer->state;
    }
  }
  save.world.tick = m_state.tick;
  for (std::size_t i = 0; i < m_state.resources.size(); ++i)
  {
    save.world.resources.push_back(ResourceSave{m_state.resources[i].name, m_state.resources[i].amount, m_income[i].remainder});
  }
  for (const ObjectiveState& objective : m_state.objectives)
  {
    save.world.objectives.push_back(ObjectiveSave{objective.text, objective.complete});
  }
  return save;
}

World::Flyer* World::FindFlyer(ObjectId _ship) noexcept
{
  const auto found = std::ranges::find(m_flyers, _ship, [](const Flyer& _flyer) { return _flyer.body.object; });
  return found != m_flyers.end() ? &*found : nullptr;
}

const World::Flyer* World::FindFlyer(ObjectId _ship) const noexcept
{
  const auto found = std::ranges::find(m_flyers, _ship, [](const Flyer& _flyer) { return _flyer.body.object; });
  return found != m_flyers.end() ? &*found : nullptr;
}

} // namespace Outpost
