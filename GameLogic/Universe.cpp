#include "Universe.h"

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

[[nodiscard]] double Distance(const UniversePosition& _a, const UniversePosition& _b) noexcept
{
  const double x = _a.x - _b.x;
  const double y = _a.y - _b.y;
  const double z = _a.z - _b.z;
  return std::sqrt(x * x + y * y + z * z);
}

/// A share of the way along a polyline of three points.
[[nodiscard]] UniversePosition Along(const std::array<UniversePosition, 3>& _points, double _metres) noexcept
{
  for (std::size_t i = 0; i + 1 < _points.size(); ++i)
  {
    const double length = Distance(_points[i], _points[i + 1]);
    if (_metres <= length || i + 2 == _points.size())
    {
      const double share = length > 0.0 ? std::clamp(_metres / length, 0.0, 1.0) : 1.0;
      const UniversePosition& a = _points[i];
      const UniversePosition& b = _points[i + 1];
      return UniversePosition{a.x + (b.x - a.x) * share, a.y + (b.y - a.y) * share, a.z + (b.z - a.z) * share};
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
  box.center = UniversePosition{_state.position.x + static_cast<double>(offset.x), _state.position.y + static_cast<double>(offset.y),
                                _state.position.z + static_cast<double>(offset.z)};
  box.orientation = _state.orientation;
  box.velocityMetresPerSecond = _state.velocityMetresPerSecond;
  return box;
}

} // namespace

bool Universe::LoadContent(const std::filesystem::path& _assetRoot, std::string& _outError)
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
  auto combat = std::make_shared<CombatContent>();
  if (!LoadCombatContent(_assetRoot, scene, *combat, _outError))
  {
    return false;
  }
  const std::vector<std::string> mounts = ResolveMounts(ships, scene, combat->hulls);
  if (!mounts.empty())
  {
    _outError = mounts.front();
    return false;
  }
  for (const SceneInstance& instance : scene.instances)
  {
    if (!instance.pilot.empty() && combat->pilots.FindPilot(instance.pilot) == nullptr)
    {
      _outError = "Scene.txt: instance '" + instance.name + "' names pilot '" + instance.pilot + "', which Pilots.txt does not define";
      return false;
    }
    if (!instance.preset.empty() && combat->hulls.FindPreset(instance.preset) == nullptr)
    {
      _outError = "Scene.txt: instance '" + instance.name + "' names preset '" + instance.preset + "', which Assets/Hulls does not hold";
      return false;
    }
    const DamagePreset* preset = combat->hulls.FindPreset(instance.preset);
    const HullRecord* hull = combat->hulls.HullForModel(instance.model);
    if (preset != nullptr && (hull == nullptr || hull->name != preset->hull))
    {
      _outError = "Scene.txt: instance '" + instance.name + "' takes preset '" + instance.preset + "', which is for another hull";
      return false;
    }
  }
  Load(scene, ships, system, std::move(combat));
  if (!scene.playerShip.empty() && m_playerShip == NO_OBJECT)
  {
    _outError = "player ship '" + scene.playerShip + "' has no ship archetype to fly with";
    return false;
  }
  return true;
}

void Universe::Load(const SceneDefinition& _scene, const ShipContent& _ships, const SystemContent& _system,
                    std::shared_ptr<const CombatContent> _combat)
{
  m_state = UniverseSnapshot{};
  m_scene = _scene;
  m_ships = _ships;
  m_gone.clear();
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
    const UniverseObject& object = m_state.objects[i];
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
    flyer.baseSpec = flyer.body.spec;
    flyer.mountRegions = _ships.MountRegions(archetype->name);
    if (_combat != nullptr)
    {
      if (const HullRecord* hull = _combat->hulls.HullForModel(object.model))
      {
        flyer.hull = static_cast<int>(hull - _combat->hulls.hulls.data());
      }
      const SceneInstance* instance = _scene.FindInstance(object.name);
      const PilotRecord* pilot = instance != nullptr ? _combat->pilots.FindPilot(instance->pilot) : nullptr;
      if (pilot != nullptr && static_cast<int>(i) != playerIndex)
      {
        // Seeded from the ship's identity, so that a run repeats exactly (R16).
        flyer.ai = Ai(*pilot, 0x5EED'0000ull + object.id);
      }
    }
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
  m_combat.Load(std::move(_combat), _scene, m_state.objects);
  if (m_combat.Active())
  {
    // A ship with a hull has its health measured, not authored (GameDesign.md §3.3).
    for (const ShipState& ship : std::vector<ShipState>(m_state.ships))
    {
      Measure(ship.object);
    }
  }
}

void Universe::Tick()
{
  ++m_state.tick;
  PayIncome();
  std::vector<PendingShots> shots;
  FlyShips(shots);
  Fight(shots);
  m_combat.StepWrecks(TICKS_PER_SECOND);
  // A pilot that has fled far enough is gone (ch. 05 §3.9), and does not come back to this save.
  std::vector<ObjectId> fled;
  for (const Flyer& flyer : m_flyers)
  {
    if (flyer.ai.has_value() && flyer.ai->State() == AiState::Gone)
    {
      fled.push_back(flyer.body.object);
    }
  }
  for (const ObjectId object : fled)
  {
    RemoveObject(object);
  }
  if (m_state.tick % SpaceSupport::HEARTBEAT_TICKS == 0)
  {
    BeatSupport();
  }
}

void Universe::PayIncome()
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

void Universe::FlyShips(std::vector<PendingShots>& _outShots)
{
  const std::uint64_t tick = m_state.tick;
  // What every pilot sees: the ships as they stood at the start of the tick.
  const std::vector<AiContact> contacts = PilotContacts();
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
        // The server flies the ship: the player's commands are set aside, and none is missed. The
        // first tick it flies, the client still predicted on its own command: it has not yet heard.
        flyer.divergedAt = commanded && tick == flyer.takenAt ? tick : flyer.divergedAt;
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
      else if (flyer.ai.has_value())
      {
        AiView view;
        view.own = &flyer.state;
        view.spec = &flyer.body.spec;
        view.self = flyer.body.object;
        view.contacts = contacts;
        view.ticksPerSecond = TICKS_PER_SECOND;
        if (const ShipState* ship = ShipStateOf(flyer.body.object))
        {
          view.hullPercent = ship->hullPercent;
        }
        view.corePercent = m_combat.CoreHealthPercent(flyer.body.object);
        const LauncherMount& first = flyer.body.spec.launchers[0];
        if (first.fitted && first.archetype < m_ships.launchers.size())
        {
          const LauncherArchetype& launcher = m_ships.launchers[first.archetype];
          view.muzzleSpeedMetresPerSecond = launcher.muzzleSpeedMetresPerSecond;
          view.gunRangeMetres = launcher.muzzleSpeedMetresPerSecond * launcher.lifetimeSeconds;
        }
        command = flyer.ai->Think(view);
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
          // A docked ship rests on the pad as a resumed save places it, its charge made good.
          m_support.Docked(flyer.body.object);
          const DockingPath& path = m_support.Points(flyer.dockStation)[flyer.dockPoint].path;
          flyer.state = RestingFlightState(flyer.body.spec, Pose{path.pad, path.orientation});
          StateDock(flyer, DockPhase::Docked, DockAnswer::None);
          if (player)
          {
            m_character.baseSnapshot = ShipRecordOf(flyer.body.object);
            const SpaceSupport::Point& berth = m_support.Points(flyer.dockStation)[flyer.dockPoint];
            m_character.base =
              CharacterLocation{LocationKind::Docked, m_support.StationName(flyer.dockStation), berth.instance, berth.name, FlightState{}};
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
      if (result.fire.shotCount > 0)
      {
        PendingShots shots;
        shots.shooter = flyer.body.object;
        shots.owner = m_state.objects[flyer.objectIndex].owner;
        shots.velocity = flyer.state.velocityMetresPerSecond;
        for (std::size_t m = 0; m < MAX_LAUNCHERS; ++m)
        {
          shots.launchers[m] = flyer.body.spec.launchers[m].archetype;
        }
        shots.fire = result.fire;
        _outShots.push_back(shots);
      }
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

void Universe::SetPose(Flyer& _flyer)
{
  UniverseObject& object = m_state.objects[_flyer.objectIndex];
  const Pose pose{_flyer.state.position, _flyer.state.orientation};
  if (!(object.pose == pose))
  {
    object.pose = pose;
    object.poseTick = m_state.tick;
    ++m_state.posesRevision;
  }
}

void Universe::RecordContact(const Flyer& _flyer, const ShipTickResult& _result, float _bodyMass)
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

bool Universe::SubmitCommand(const FlightInputMessage& _input)
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

bool Universe::Direct(ObjectId _ship, const Manoeuvre& _manoeuvre)
{
  Flyer* flyer = FindFlyer(_ship);
  if (flyer == nullptr || _ship == m_playerShip)
  {
    return false;
  }
  flyer->manoeuvre = _manoeuvre;
  return true;
}

const FlightState* Universe::Flight(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr ? &flyer->state : nullptr;
}

std::uint64_t Universe::DivergedAt(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr ? flyer->divergedAt : 0;
}

bool Universe::Arrived(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr && flyer->arrived;
}

void Universe::BeatSupport()
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

void Universe::StateDock(Flyer& _flyer, DockPhase _phase, DockAnswer _answer, std::string _reason)
{
  const bool freed = _flyer.dock.phase != DockPhase::None && _phase == DockPhase::None;
  if (_flyer.dock.phase == DockPhase::None && _phase != DockPhase::None)
  {
    _flyer.takenAt = m_state.tick + 1;
  }
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

void Universe::AskForPoint(Flyer& _flyer)
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

void Universe::Clear(Flyer& _flyer, std::size_t _point)
{
  _flyer.dockPoint = _point;
  // Straight to the entry when nothing immovable is in the way; otherwise by the docking axis.
  const DockingPath& path = m_support.Points(_flyer.dockStation)[_point].path;
  const UniversePosition& from = _flyer.state.position;
  const XMFLOAT3 toEntry{static_cast<float>(path.entry.x - from.x), static_cast<float>(path.entry.y - from.y),
                         static_cast<float>(path.entry.z - from.z)};
  SweepHit hit;
  _flyer.axisPoint.reset();
  if (SweepSphere(from, toEntry, _flyer.body.radiusMetres, m_staticBodies, _flyer.body.object, hit))
  {
    const double length = Distance(path.entry, path.approach);
    const double share = length > 0.0 ? static_cast<double>(AXIS_STANDOFF_METRES) / length : 0.0;
    _flyer.axisPoint =
      UniversePosition{path.entry.x + (path.entry.x - path.approach.x) * share, path.entry.y + (path.entry.y - path.approach.y) * share,
                       path.entry.z + (path.entry.z - path.approach.z) * share};
  }
  StateDock(_flyer, DockPhase::Staging, DockAnswer::Proceed);
}

void Universe::StartMover(Flyer& _flyer, const UniversePosition& _from, const UniversePosition& _via, const UniversePosition& _to,
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

bool Universe::StepMover(Flyer& _flyer)
{
  const Mover& mover = _flyer.mover;
  const double share = std::min(1.0, static_cast<double>(m_state.tick - mover.startTick) / static_cast<double>(mover.durationTicks));
  const double travelled = mover.outward ? share * share : share * share * (3.0 - 2.0 * share);
  const UniversePosition before = _flyer.state.position;
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

void Universe::PlaceDocked(Flyer& _flyer, std::size_t _station, std::size_t _point)
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

void Universe::Release(Flyer& _flyer)
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

bool Universe::Command(const DockCommandMessage& _command)
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

DockStatusState Universe::DockStatus(ObjectId _ship) const
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr ? flyer->dock : DockStatusState{};
}

bool Universe::Protected(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr && Protects(flyer->dock.phase);
}

bool Universe::TakeSaveRequest() noexcept
{
  return std::exchange(m_saveRequested, false);
}

ShipState* Universe::ShipStateOf(ObjectId _ship) noexcept
{
  const auto found = std::ranges::find(m_state.ships, _ship, &ShipState::object);
  return found != m_state.ships.end() ? &*found : nullptr;
}

const ShipState* Universe::ShipStateOf(ObjectId _ship) const noexcept
{
  const auto found = std::ranges::find(m_state.ships, _ship, &ShipState::object);
  return found != m_state.ships.end() ? &*found : nullptr;
}

CharacterShip Universe::ShipRecordOf(ObjectId _ship) const
{
  CharacterShip record;
  if (const UniverseObject* object = m_state.FindObject(_ship))
  {
    record.instance = object->name;
  }
  if (const ShipState* ship = ShipStateOf(_ship))
  {
    record.hullPercent = ship->hullPercent;
    record.modules = ship->modules;
  }
  const Neuron::VoxelMask* mask = m_combat.Mask(_ship);
  const VoxelArchetype* archetype = m_combat.ArchetypeOf(_ship);
  if (mask != nullptr && archetype != nullptr)
  {
    record.absent = Neuron::AbsentSpans(*mask, archetype->full);
  }
  return record;
}

bool Universe::NewGame(std::string& _outError)
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
  m_character.repairKits = m_system.startRepairKits;
  PlaceDocked(*flyer, station, answer.point);
  const SpaceSupport::Point& berth = m_support.Points(station)[answer.point];
  m_character.base = CharacterLocation{LocationKind::Docked, m_system.startStation, berth.instance, berth.name, FlightState{}};
  return true;
}

bool Universe::Restore(const SaveGame& _save, std::string& _outError)
{
  Flyer* flyer = FindFlyer(m_playerShip);
  const UniverseObject* object = m_state.FindObject(m_playerShip);
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
  // Everything is checked before anything changes, so a refused save leaves the universe as it was.
  std::vector<std::size_t> resourceIndices;
  for (const ResourceSave& resource : _save.universe.resources)
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
  for (const ObjectiveSave& objective : _save.universe.objectives)
  {
    const auto found = std::ranges::find(m_state.objectives, objective.text, &ObjectiveState::text);
    if (found == m_state.objectives.end())
    {
      _outError = "the save names unknown objective '" + objective.text + "'";
      return false;
    }
    objectiveIndices.push_back(static_cast<std::size_t>(found - m_state.objectives.begin()));
  }
  // Masks and the objects that are gone, against this content (ADR-012).
  std::vector<std::pair<ObjectId, Neuron::VoxelMask>> masks;
  const auto maskFrom = [&](ObjectId _object, const std::vector<Neuron::CellSpan>& _absent, std::string_view _what)
  {
    const VoxelArchetype* archetype = m_combat.ArchetypeOf(_object);
    Neuron::VoxelMask mask;
    if (archetype == nullptr || !Neuron::MaskFromAbsentSpans(archetype->full, _absent, mask))
    {
      _outError = "the save's mask of " + std::string(_what) + " does not fit its model";
      return false;
    }
    masks.emplace_back(_object, std::move(mask));
    return true;
  };
  if (m_combat.Active() && !maskFrom(m_playerShip, character.ship.absent, "the player's ship"))
  {
    return false;
  }
  for (const MaskSave& saved : _save.universe.masks)
  {
    const ObjectId object = FindByName(saved.instance);
    if (object == NO_OBJECT || object == m_playerShip || !maskFrom(object, saved.absent, "'" + saved.instance + "'"))
    {
      _outError = _outError.empty() ? "the save names unknown object '" + saved.instance + "'" : _outError;
      return false;
    }
  }
  for (const std::string& name : _save.universe.gone)
  {
    const ObjectId object = FindByName(name);
    if (object == NO_OBJECT || object == m_playerShip)
    {
      _outError = "the save has '" + name + "' gone, which this content does not place or which the player flies";
      return false;
    }
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

  m_state.tick = _save.universe.tick;
  for (std::size_t i = 0; i < resourceIndices.size(); ++i)
  {
    m_state.resources[resourceIndices[i]].amount = _save.universe.resources[i].amount;
    m_income[resourceIndices[i]].remainder = _save.universe.resources[i].remainder;
    m_state.resourceTicks[resourceIndices[i]] = m_state.tick;
  }
  for (std::size_t i = 0; i < objectiveIndices.size(); ++i)
  {
    m_state.objectives[objectiveIndices[i]].complete = _save.universe.objectives[i].complete;
  }
  m_character = character;
  for (const std::string& name : _save.universe.gone)
  {
    RemoveObject(FindByName(name));
  }
  for (const auto& [object, mask] : masks)
  {
    static_cast<void>(m_combat.SetMask(object, mask));
    Measure(object);
  }
  if (ShipState* ship = ShipStateOf(m_playerShip); ship != nullptr && !m_combat.Active())
  {
    ship->hullPercent = character.ship.hullPercent;
    ship->modules = character.ship.modules;
  }
  m_commands.clear();
  m_heldCommand = ShipCommand{};
  m_commandsStarted = false;
  if (station != SpaceSupport::NO_POINT)
  {
    flyer = FindFlyer(m_playerShip);
    PlaceDocked(*flyer, station, point);
  }
  else
  {
    m_support.Release(m_playerShip);
    flyer = FindFlyer(m_playerShip); // removals above may have moved it
    flyer->state = character.location.flight;
    if (flyer->state.shield < 0.0f)
    {
      // A save from before shields: full, up, and the guns ready.
      const FlightState rested = RestingFlightState(flyer->body.spec, Pose{});
      flyer->state.shield = rested.shield;
      flyer->state.shieldUp = true;
      flyer->state.shieldRebuildTicks = 0;
      flyer->state.launcherClockTicks = rested.launcherClockTicks;
    }
    flyer->manoeuvre = Manoeuvre::FreeFlight();
    flyer->dock = DockStatusState{};
    flyer->dockStation = SpaceSupport::NO_POINT;
    flyer->dockPoint = SpaceSupport::NO_POINT;
    SetPose(*flyer);
  }
  // Every object's pose is stated as of the restored tick, so the client's newest-wins copies take it.
  for (UniverseObject& each : m_state.objects)
  {
    each.poseTick = m_state.tick;
  }
  for (ShipMotionState& motion : m_state.motions)
  {
    motion.tick = m_state.tick;
  }
  return true;
}

SaveGame Universe::MakeSave() const
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
  save.universe.tick = m_state.tick;
  for (std::size_t i = 0; i < m_state.resources.size(); ++i)
  {
    save.universe.resources.push_back(ResourceSave{m_state.resources[i].name, m_state.resources[i].amount, m_income[i].remainder});
  }
  for (const ObjectiveState& objective : m_state.objectives)
  {
    save.universe.objectives.push_back(ObjectiveSave{objective.text, objective.complete});
  }
  for (const UniverseObject& object : m_state.objects)
  {
    const Neuron::VoxelMask* mask = m_combat.Mask(object.id);
    const Neuron::VoxelMask* initial = m_combat.Initial(object.id);
    if (object.id != m_playerShip && mask != nullptr && initial != nullptr && !(*mask == *initial))
    {
      save.universe.masks.push_back(MaskSave{object.name, Neuron::AbsentSpans(*mask, m_combat.ArchetypeOf(object.id)->full)});
    }
  }
  save.universe.gone = m_gone;
  return save;
}

std::vector<AiContact> Universe::PilotContacts() const
{
  std::vector<AiContact> contacts;
  for (const Flyer& flyer : m_flyers)
  {
    const UniverseObject& object = m_state.objects[flyer.objectIndex];
    contacts.push_back(AiContact{object.id, object.owner, flyer.state.position, flyer.state.velocityMetresPerSecond,
                                 flyer.state.orientation, flyer.dock.phase == DockPhase::None});
  }
  return contacts;
}

void Universe::Fight(const std::vector<PendingShots>& _shots)
{
  const std::uint64_t tick = m_state.tick;
  for (const PendingShots& shots : _shots)
  {
    for (std::size_t k = 0; k < shots.fire.shotCount; ++k)
    {
      const Shot& shot = shots.fire.shots[k];
      m_combat.Fire(shots.shooter, shots.owner, shot, shots.launchers[shot.mount], m_ships, shots.velocity, TICKS_PER_SECOND, tick);
    }
  }
  if (m_combat.BoltCount() == 0)
  {
    return;
  }
  // Everything a bolt can meet, where the tick left it.
  std::vector<CombatTarget> targets;
  targets.reserve(m_state.objects.size());
  for (const UniverseObject& object : m_state.objects)
  {
    CombatTarget target;
    target.object = object.id;
    target.pose = object.pose;
    target.scale = object.scale;
    target.owner = object.owner;
    if (Flyer* flyer = FindFlyer(object.id))
    {
      target.isProtected = Protects(flyer->dock.phase);
      target.flight = &flyer->state;
      target.spec = &flyer->body.spec;
      target.shieldRadiusMetres = flyer->body.radiusMetres;
    }
    targets.push_back(target);
  }
  const std::vector<BoltHit> hits = m_combat.StepBolts(targets, m_ships, TICKS_PER_SECOND, tick);
  std::vector<ObjectId> carved;
  for (const BoltHit& hit : hits)
  {
    if (!hit.harmful)
    {
      continue;
    }
    Flyer* flyer = FindFlyer(hit.target);
    if (flyer != nullptr && (hit.shieldDamage > 0.0f || hit.energyDamage > 0.0f))
    {
      if (hit.shield)
      {
        static_cast<void>(DamageShield(flyer->body.spec, TICKS_PER_SECOND, hit.shieldDamage, flyer->state));
      }
      flyer->state.powerPool = std::max(0.0f, flyer->state.powerPool - hit.energyDamage);
      if (hit.target == m_playerShip)
      {
        flyer->divergedAt = tick; // the client could not have predicted the hit
      }
    }
    if (!hit.carved.removed.empty() && std::ranges::find(carved, hit.target) == carved.end())
    {
      carved.push_back(hit.target);
    }
  }
  for (const ObjectId object : carved)
  {
    Measure(object);
    if (m_combat.CoreStopped(object))
    {
      Destroy(object);
    }
  }
}

void Universe::Measure(ObjectId _ship)
{
  const VoxelArchetype* archetype = m_combat.ArchetypeOf(_ship);
  const HullRecord* hull = archetype != nullptr ? m_combat.Content()->HullOf(*archetype) : nullptr;
  ShipState* ship = ShipStateOf(_ship);
  if (hull == nullptr || ship == nullptr)
  {
    return;
  }
  ship->hullPercent = m_combat.HullPercent(_ship);
  ship->modules = m_combat.Modules(_ship);
  if (Flyer* flyer = FindFlyer(_ship))
  {
    // The condition is read from the health as stated, as the client reads it (plan task 3.6).
    const ShipCondition condition = ConditionOf(m_combat.Content()->hulls, *hull, ship->modules, flyer->mountRegions);
    flyer->body.spec = ApplyCondition(flyer->baseSpec, condition);
  }
}

void Universe::Destroy(ObjectId _ship)
{
  if (_ship == m_playerShip)
  {
    if (Flyer* flyer = FindFlyer(_ship))
    {
      ReturnToBase(*flyer);
    }
    return;
  }
  const UniverseObject* object = m_state.FindObject(_ship);
  if (object == nullptr)
  {
    return;
  }
  const Flyer* flyer = FindFlyer(_ship);
  m_combat.BreakUp(_ship, object->pose, object->scale, flyer != nullptr ? flyer->state.velocityMetresPerSecond : XMFLOAT3{});
  RemoveObject(_ship);
}

void Universe::RemoveObject(ObjectId _object)
{
  const auto found = std::ranges::find(m_state.objects, _object, &UniverseObject::id);
  if (found == m_state.objects.end() || _object == m_playerShip)
  {
    return;
  }
  const auto index = static_cast<std::size_t>(found - m_state.objects.begin());
  m_gone.push_back(found->name);
  m_state.objects.erase(found);
  std::erase_if(m_state.ships, [_object](const ShipState& _ship) { return _ship.object == _object; });
  for (std::size_t f = 0; f < m_flyers.size();)
  {
    if (m_flyers[f].body.object == _object)
    {
      m_flyers.erase(m_flyers.begin() + static_cast<std::ptrdiff_t>(f));
      m_state.motions.erase(m_state.motions.begin() + static_cast<std::ptrdiff_t>(f));
      continue;
    }
    if (m_flyers[f].objectIndex > index)
    {
      --m_flyers[f].objectIndex;
    }
    ++f;
  }
  std::erase_if(m_staticBodies, [_object](const CollisionBox& _body) { return _body.object == _object; });
  m_support.Release(_object);
  m_combat.Remove(_object);
  ++m_state.objectsRevision;
}

void Universe::ReturnToBase(Flyer& _flyer)
{
  // D1: the ship breaks up where it died, and the player is back at the last base with the ship as
  // fitted there; cargo is lost.
  const UniverseObject& object = m_state.objects[_flyer.objectIndex];
  m_combat.Shatter(object.id, object.pose, object.scale, _flyer.state.velocityMetresPerSecond);
  if (const VoxelArchetype* archetype = m_combat.ArchetypeOf(object.id))
  {
    Neuron::VoxelMask base;
    if (Neuron::MaskFromAbsentSpans(archetype->full, m_character.baseSnapshot.absent, base))
    {
      static_cast<void>(m_combat.SetMask(object.id, base));
    }
  }
  m_character.cargo.clear();
  std::size_t station = m_support.FindStation(m_character.base.station);
  std::size_t point = SpaceSupport::NO_POINT;
  if (station != SpaceSupport::NO_POINT)
  {
    const std::vector<SpaceSupport::Point>& points = m_support.Points(station);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
      if (points[i].instance == m_character.base.instance && points[i].name == m_character.base.point)
      {
        point = i;
      }
    }
  }
  m_support.Release(object.id);
  if (point == SpaceSupport::NO_POINT)
  {
    station = m_support.FindStation(m_system.startStation);
    const SpaceSupport::Answer answer =
      m_support.Request(object.id, _flyer.dockingClass, _flyer.state.position, station, true, m_state.tick);
    point = answer.point;
  }
  if (station == SpaceSupport::NO_POINT || point == SpaceSupport::NO_POINT)
  {
    return;
  }
  _flyer.manoeuvre = Manoeuvre::FreeFlight();
  m_commands.clear();
  m_heldCommand = ShipCommand{};
  m_commandsStarted = false;
  Measure(object.id);
  PlaceDocked(_flyer, station, point);
  m_saveRequested = true; // back in a berth, as on docking (D17)
}

bool Universe::UseItem(const UseItemMessage& _use)
{
  Flyer* flyer = FindFlyer(_use.ship);
  if (flyer == nullptr || _use.ship != m_playerShip || flyer->dock.phase != DockPhase::None || m_character.repairKits <= 0 ||
      !m_combat.Active())
  {
    return false;
  }
  // A kit patches structure in the field and leaves modules for a hangar (ch. 03 §3.10). A kit
  // that finds nothing to patch is not used up.
  if (m_combat.Patch(_use.ship, REPAIR_KIT_CELLS, m_state.tick) == 0)
  {
    return false;
  }
  --m_character.repairKits;
  Measure(_use.ship);
  return true;
}

std::optional<AiState> Universe::PilotState(ObjectId _ship) const noexcept
{
  const Flyer* flyer = FindFlyer(_ship);
  return flyer != nullptr && flyer->ai.has_value() ? std::optional<AiState>(flyer->ai->State()) : std::nullopt;
}

ObjectId Universe::FindByName(std::string_view _instance) const noexcept
{
  const auto found = std::ranges::find(m_state.objects, _instance, &UniverseObject::name);
  return found != m_state.objects.end() ? found->id : NO_OBJECT;
}

Universe::Flyer* Universe::FindFlyer(ObjectId _ship) noexcept
{
  const auto found = std::ranges::find(m_flyers, _ship, [](const Flyer& _flyer) { return _flyer.body.object; });
  return found != m_flyers.end() ? &*found : nullptr;
}

const Universe::Flyer* Universe::FindFlyer(ObjectId _ship) const noexcept
{
  const auto found = std::ranges::find(m_flyers, _ship, [](const Flyer& _flyer) { return _flyer.body.object; });
  return found != m_flyers.end() ? &*found : nullptr;
}

} // namespace Outpost
