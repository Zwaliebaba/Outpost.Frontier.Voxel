#include "World.h"

#include "TickClock.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Outpost
{

namespace
{

constexpr std::uint32_t TICKS_PER_SECOND = Neuron::TickClock::TICKS_PER_SECOND;

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
  Load(scene, ships);
  if (!scene.playerShip.empty() && m_playerShip == NO_OBJECT)
  {
    _outError = "player ship '" + scene.playerShip + "' has no ship archetype to fly with";
    return false;
  }
  return true;
}

void World::Load(const SceneDefinition& _scene, const ShipContent& _ships)
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
  // Every body: the immovable ones, then each ship where it stands. A ship that has moved this tick
  // is met where it went; one that has not yet, where it was.
  std::vector<CollisionBox> bodies = m_staticBodies;
  const std::size_t firstShip = bodies.size();
  for (const Flyer& flyer : m_flyers)
  {
    bodies.push_back(PlaceBox(flyer.localBox, flyer.state));
  }

  for (std::size_t i = 0; i < m_flyers.size(); ++i)
  {
    Flyer& flyer = m_flyers[i];
    ShipCommand command{flyer.manoeuvre, FlightControls{}};
    if (flyer.body.object == m_playerShip)
    {
      // Commands are in tick order and none is older than this tick, so this tick's is the first.
      if (!m_commands.empty() && m_commands.front().first == tick)
      {
        m_heldCommand = m_commands.front().second;
      }
      else if (m_commandsStarted)
      {
        flyer.divergedAt = tick; // the client predicted this tick on a command the server never saw
      }
      const auto later = std::ranges::find_if(m_commands, [tick](const auto& _entry) { return _entry.first > tick; });
      m_commands.erase(m_commands.begin(), later);
      command = m_heldCommand;
    }

    const ShipTickResult result = StepShip(flyer.body, command, bodies, TICKS_PER_SECOND, flyer.state);
    flyer.arrived = result.arrived;
    if (result.contact)
    {
      const auto body = std::ranges::find(bodies, result.contactObject, &CollisionBox::object);
      RecordContact(flyer, result, body != bodies.end() ? body->massKilograms : 0.0f);
      if (result.contactMoves)
      {
        flyer.divergedAt = tick; // the prediction does not know where other ships are
      }
    }
    bodies[firstShip + i] = PlaceBox(flyer.localBox, flyer.state);

    WorldObject& object = m_state.objects[flyer.objectIndex];
    const Pose pose{flyer.state.position, flyer.state.orientation};
    if (!(object.pose == pose))
    {
      object.pose = pose;
      object.poseTick = tick;
      ++m_state.posesRevision;
    }
    ShipMotionState motion{object.id,
                           tick,
                           result.controls.engineOn ? result.controls.throttle : 0.0f,
                           flyer.state.SpeedMetresPerSecond(),
                           flyer.state.thrusterActive,
                           result.controls.engineOn,
                           flyer.state.cruise};
    m_state.motions[i] = motion;
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
