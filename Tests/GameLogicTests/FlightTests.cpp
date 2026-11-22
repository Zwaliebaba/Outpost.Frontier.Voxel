#include "Collision.h"
#include "Commands.h"
#include "Messages.h"
#include "Observer.h"
#include "SceneDefinition.h"
#include "ShipArchetype.h"
#include "TickClock.h"
#include "World.h"

#include "CppUnitTest.h"

#include <DirectXMath.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The light fighter of Assets/Content/Ships.txt, the blueprint's worked example (ch. 02 §4).
constexpr std::string_view SHIPS = R"(engine name=LightEngine thrust=48000 drag=599 reverse=0.2 chargetime=5 cruisepower=20
powerplant name=LightPowerPlant capacity=1000 charge=100 thrustcapacity=1000 thrustcharge=100
thruster name=BasicThruster force=72000 drain=165
ship name=Fighter model=Fighter mass=100 inertia=2800,2800,1000 drag=1 torque=24000,24000,58000 angulardrag=15000,15000,35000 strafe=20000 strafedrain=2 maxbank=30 engine=LightEngine powerplant=LightPowerPlant thruster=BasicThruster
)";

// A fighter 20 x 10 x 40 m (VOX x, z, y), a wingman, and a wall 200 m square and 4 m thick whose
// near face is 1,000 m ahead of the fighter's nose (+Z).
constexpr std::string_view SCENE = R"(camera target=0,0,0 yaw=0 pitch=20 distance=300 fov=45
model name=Fighter file=Models/Fighter.vox
model name=Wall file=Models/Wall.vox
instance name=Lead model=Fighter position=0,0,0
instance name=Wing model=Fighter position=200,0,0
instance name=Wall model=Wall position=0,0,1002
ship instance=Lead callsign=E-1 class=Escort hull=100
ship instance=Wing callsign=E-2 class=Escort hull=100
player instance=Lead
)";

constexpr std::uint32_t TICKS_PER_SECOND = Neuron::TickClock::TICKS_PER_SECOND;
constexpr Outpost::ObjectId LEAD = 1;
constexpr Outpost::ObjectId WING = 2;
constexpr Outpost::ObjectId WALL = 3;

[[nodiscard]] std::wstring Wide(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

[[nodiscard]] Outpost::SceneDefinition Scene(std::string_view _text = SCENE)
{
  Outpost::SceneDefinition scene;
  std::string error;
  Assert::IsTrue(Outpost::ParseScene(_text, scene, error), Wide(error).c_str());
  scene.FindModel("Fighter")->SetSize(Neuron::Int3{20, 40, 10});
  if (Outpost::ModelAsset* wall = scene.FindModel("Wall"))
  {
    wall->SetSize(Neuron::Int3{200, 4, 200});
  }
  return scene;
}

[[nodiscard]] Outpost::ShipContent Ships()
{
  Outpost::ShipContent content;
  std::string error;
  Assert::IsTrue(Outpost::ParseShipContent(SHIPS, "Ships.txt", content, error), Wide(error).c_str());
  return content;
}

[[nodiscard]] Outpost::World LoadedWorld(std::string_view _scene = SCENE)
{
  Outpost::World world;
  world.Load(Scene(_scene), Ships());
  return world;
}

/// The player's command for a tick of the scripted flight: throttle up, a turn, the thruster, a
/// strafe, the engine killed and restarted.
[[nodiscard]] Outpost::ShipCommand Timeline(std::uint64_t _tick)
{
  Outpost::ShipCommand command;
  Outpost::FlightControls& wishes = command.wishes;
  wishes.throttle = _tick <= 240 ? 1.0f : 0.4f;
  if (_tick >= 30 && _tick < 150)
  {
    wishes.steering = DirectX::XMFLOAT3{0.2f, -0.6f, 0.3f};
  }
  wishes.thruster = _tick >= 150 && _tick < 210;
  wishes.strafe = _tick >= 210 && _tick < 260 ? Outpost::StrafeDirection::Up : Outpost::StrafeDirection::None;
  wishes.engineOn = _tick < 300 || _tick >= 360;
  return command;
}

/// Runs the world as the executable would at a render rate: each frame the clock turns wall time
/// into ticks, and each tick runs on the command for it.
[[nodiscard]] std::vector<Outpost::Pose> FlyAt(double _framesPerSecond, std::uint64_t _ticks)
{
  Outpost::World world = LoadedWorld();
  Neuron::TickClock clock;
  std::vector<Outpost::Pose> trajectory;
  double carried = 0.0;
  while (world.TickCount() < _ticks)
  {
    carried += 1'000'000.0 / _framesPerSecond;
    const auto whole = static_cast<std::uint64_t>(carried);
    carried -= static_cast<double>(whole);
    const std::uint32_t due = clock.Advance(whole);
    for (std::uint32_t i = 0; i < due && world.TickCount() < _ticks; ++i)
    {
      const std::uint64_t next = world.TickCount() + 1;
      Assert::IsTrue(world.SubmitCommand(Outpost::FlightInputMessage{next, LEAD, Timeline(next)}));
      world.Tick();
      trajectory.push_back(world.State().FindObject(LEAD)->pose);
    }
  }
  return trajectory;
}

[[nodiscard]] double Distance(const Outpost::WorldPosition& _a, const Outpost::WorldPosition& _b)
{
  const double dx = _a.x - _b.x;
  const double dy = _a.y - _b.y;
  const double dz = _a.z - _b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Whether a sphere at _center is clear of a box grown by the radius, to within a millimetre.
[[nodiscard]] bool Outside(const Outpost::CollisionBox& _box, const Outpost::WorldPosition& _center, float _radius)
{
  const DirectX::XMVECTOR local = DirectX::XMVector3Rotate(DirectX::XMVectorSet(static_cast<float>(_center.x - _box.center.x),
                                                                                static_cast<float>(_center.y - _box.center.y),
                                                                                static_cast<float>(_center.z - _box.center.z), 0.0f),
                                                           DirectX::XMQuaternionConjugate(DirectX::XMLoadFloat4(&_box.orientation)));
  DirectX::XMFLOAT3 p{};
  DirectX::XMStoreFloat3(&p, local);
  const float slack = -0.001f;
  return std::abs(p.x) - _box.halfExtentsMetres.x - _radius >= slack || std::abs(p.y) - _box.halfExtentsMetres.y - _radius >= slack ||
         std::abs(p.z) - _box.halfExtentsMetres.z - _radius >= slack;
}

[[nodiscard]] float FighterRadius()
{
  Outpost::WorldObject fighter;
  fighter.model = "Fighter";
  return Outpost::BoundingRadius(Outpost::BoxForObject(Scene(), fighter));
}

/// What flying the lead fighter at the wall under one command did.
struct WallRun
{
  bool contact = false;
  bool alwaysOutside = true;
  float fastestApproach = 0.0f;
};

[[nodiscard]] WallRun FlyIntoWall(const Outpost::ShipCommand& _command, int _ticks)
{
  Outpost::World world = LoadedWorld();
  const Outpost::CollisionBox& wall = world.StaticBodies().front();
  const float radius = world.Flight(LEAD) != nullptr ? FighterRadius() : 0.0f;
  WallRun run;
  for (int i = 0; i < _ticks; ++i)
  {
    Assert::IsTrue(world.SubmitCommand(Outpost::FlightInputMessage{world.TickCount() + 1, LEAD, _command}));
    world.Tick();
    const Outpost::FlightState& flight = *world.Flight(LEAD);
    run.alwaysOutside = run.alwaysOutside && Outside(wall, flight.position, radius);
    run.fastestApproach = std::max(run.fastestApproach, flight.velocityMetresPerSecond.z);
  }
  run.contact = !world.Contacts().empty();
  return run;
}

[[nodiscard]] std::optional<Outpost::Message> NextMessage(Neuron::LoopbackTransport& _transport, Neuron::Peer _to)
{
  std::optional<Neuron::Packet> packet = _transport.Receive(_to);
  if (!packet.has_value())
  {
    return std::nullopt;
  }
  auto message = Outpost::Decode(packet->bytes);
  Assert::IsTrue(message.has_value(), L"every message decodes");
  return *message;
}

} // namespace

TEST_CLASS(FlightTests)
{
public:
  TEST_METHOD(EveryShipWithAnArchetypeFliesAndThePlayerHasOne)
  {
    const Outpost::World world = LoadedWorld();
    Assert::AreEqual(LEAD, world.PlayerShip());
    Assert::IsNotNull(world.Flight(LEAD));
    Assert::IsNotNull(world.Flight(WING));
    Assert::IsNull(world.Flight(WALL), L"the wall has no archetype: it is a body, not a ship");
    Assert::AreEqual(std::size_t{1}, world.StaticBodies().size());
    Assert::AreEqual(std::size_t{2}, world.State().motions.size());
  }

  TEST_METHOD(AnInputTimelineFliesTheSamePathAtEveryFrameRate)
  {
    // Plan task 1.4: at 30, 60 and 144 frames a second the client runs two, one, or no ticks a
    // frame; the ticks, and so the path, do not change.
    constexpr std::uint64_t TICKS = 420;
    const std::vector<Outpost::Pose> at60 = FlyAt(60.0, TICKS);
    const std::vector<Outpost::Pose> at30 = FlyAt(30.0, TICKS);
    const std::vector<Outpost::Pose> at144 = FlyAt(144.0, TICKS);
    Assert::AreEqual(static_cast<std::size_t>(TICKS), at60.size());
    Assert::IsTrue(at60 == at30, L"30 fps flies the same path, tick for tick");
    Assert::IsTrue(at60 == at144, L"144 fps flies the same path, tick for tick");
    Assert::IsTrue(Distance(at60.front().position, at60.back().position) > 200.0, L"the timeline really flew");
  }

  TEST_METHOD(AnotherShipFliesToAPointUnderAGoToDirective)
  {
    Outpost::World world = LoadedWorld();
    const Outpost::WorldPosition target{200.0, 150.0, -400.0};
    Assert::IsTrue(world.Direct(WING, Outpost::Manoeuvre::GoTo(target, 30.0f)));
    Assert::IsFalse(world.Arrived(WING));
    for (std::uint32_t i = 0; i < 30 * TICKS_PER_SECOND && !world.Arrived(WING); ++i)
    {
      world.Tick();
    }
    Assert::IsTrue(world.Arrived(WING), L"it arrives within 30 s");
    const double distance = Distance(world.Flight(WING)->position, target);
    Assert::AreEqual(30.0, distance, static_cast<double>(Outpost::ARRIVAL_TOLERANCE_METRES), L"at its stand-off");
    Assert::IsTrue(world.State().FindObject(WING)->pose.position == world.Flight(WING)->position, L"its object moved with it");
  }

  TEST_METHOD(OnlyThePlayersShipTakesCommandsAndOnlyForTicksToCome)
  {
    Outpost::World world = LoadedWorld();
    world.Tick();
    Assert::IsFalse(world.SubmitCommand(Outpost::FlightInputMessage{2, WING, {}}), L"not the player's ship");
    Assert::IsFalse(world.SubmitCommand(Outpost::FlightInputMessage{1, LEAD, {}}), L"tick 1 has run");
    Assert::IsFalse(world.SubmitCommand(Outpost::FlightInputMessage{1 + 1 + Outpost::World::MAX_COMMAND_LEAD_TICKS, LEAD, {}}));
    Assert::IsTrue(world.SubmitCommand(Outpost::FlightInputMessage{2, LEAD, {}}));
    Assert::IsFalse(world.Direct(LEAD, Outpost::Manoeuvre::Hold({})), L"the player's ship takes its manoeuvre in its commands");
  }

  TEST_METHOD(AMissingCommandHoldsTheLastAndMarksTheTickDiverged)
  {
    Outpost::World world = LoadedWorld();
    Outpost::ShipCommand full;
    full.wishes.throttle = 1.0f;
    Assert::IsTrue(world.SubmitCommand(Outpost::FlightInputMessage{1, LEAD, full}));
    world.Tick();
    Assert::AreEqual(std::uint64_t{0}, world.DivergedAt(LEAD));
    world.Tick(); // no command for tick 2
    Assert::AreEqual(std::uint64_t{2}, world.DivergedAt(LEAD));
    Assert::IsTrue(world.State().FindMotion(LEAD)->throttle == 1.0f, L"tick 2 flew on tick 1's command");
  }

  TEST_METHOD(AShipFlownIntoAWallStopsOutsideItAtEverySpeed)
  {
    // Plan task 1.11, through the world: the engine at full throttle (80 m/s) and the thruster
    // (200 m/s). Every tick the ship's sphere is outside the wall. Cruise speed and the 1,000 m/s
    // limit are swept in GameCoreTests (ShipFlightTests), where a ship can be started at speed.
    Outpost::ShipCommand engine;
    engine.wishes.throttle = 1.0f;
    Outpost::ShipCommand thruster = engine;
    thruster.wishes.thruster = true;
    for (const Outpost::ShipCommand& command : {engine, thruster})
    {
      const WallRun run = FlyIntoWall(command, 30 * static_cast<int>(TICKS_PER_SECOND));
      Assert::IsTrue(run.contact, L"it met the wall");
      Assert::IsTrue(run.alwaysOutside, L"and never entered it");
      Assert::IsTrue(run.fastestApproach > (command.wishes.thruster ? 150.0f : 75.0f), L"at the regime's speed");
    }
  }

  TEST_METHOD(CruiseIsRefusedWhileTheWallIsAhead)
  {
    Outpost::World world = LoadedWorld();
    Outpost::ShipCommand cruise;
    cruise.wishes.throttle = 1.0f;
    cruise.wishes.cruise = true;
    bool everCruised = false;
    for (std::uint64_t tick = 1; tick <= std::uint64_t{20} * TICKS_PER_SECOND; ++tick)
    {
      Assert::IsTrue(world.SubmitCommand(Outpost::FlightInputMessage{tick, LEAD, cruise}));
      world.Tick();
      everCruised = everCruised || world.Flight(LEAD)->cruise != Outpost::CruiseState::Off;
      Assert::IsTrue(Outside(world.StaticBodies().front(), world.Flight(LEAD)->position, FighterRadius()));
    }
    // Charging starts while the wall is more than 600 m ahead, and is dropped once it is not.
    Assert::IsTrue(everCruised, L"cruise began to charge while the way was clear");
    Assert::IsTrue(world.Flight(LEAD)->cruise == Outpost::CruiseState::Off, L"and was refused as the wall came within 600 m");
    Assert::IsFalse(world.Contacts().empty(), L"at engine speed it still met the wall");
  }

  TEST_METHOD(AContactIsRecordedOnceWithTheBlueprintsDamage)
  {
    Outpost::World world = LoadedWorld();
    Outpost::ShipCommand engine;
    engine.wishes.throttle = 1.0f;
    for (std::uint64_t tick = 1; tick <= std::uint64_t{30} * TICKS_PER_SECOND; ++tick)
    {
      Assert::IsTrue(world.SubmitCommand(Outpost::FlightInputMessage{tick, LEAD, engine}));
      world.Tick();
    }
    Assert::IsFalse(world.Contacts().empty());
    const Outpost::ContactRecord& first = world.Contacts().front();
    Assert::AreEqual(LEAD, first.ship);
    Assert::AreEqual(WALL, first.body);
    Assert::AreEqual(0.5f * first.relativeSpeedMetresPerSecond, first.damageToShip, 1.0e-4f, L"the wall is immovable: full rate");
    Assert::AreEqual(0.0f, first.damageToBody, L"ramming an immovable body does it no harm");
    for (std::size_t i = 1; i < world.Contacts().size(); ++i)
    {
      Assert::IsTrue(world.Contacts()[i].tick - world.Contacts()[i - 1].tick >= 30, L"at most once per half second per pair");
    }
  }

  TEST_METHOD(CommandsCrossTheLoopbackAndTheObserverTellsTheClientItsShip)
  {
    Outpost::World world = LoadedWorld();
    Outpost::Observer observer;
    Neuron::LoopbackTransport transport;
    observer.AssignShip(world.PlayerShip());
    Assert::IsTrue(observer.Publish(world, transport));

    bool toldOwnShip = false;
    std::size_t motions = 0;
    while (const std::optional<Outpost::Message> message = NextMessage(transport, Neuron::Peer::Client))
    {
      if (const auto* own = std::get_if<Outpost::OwnShipMessage>(&*message))
      {
        toldOwnShip = true;
        Assert::AreEqual(LEAD, own->ship);
        Assert::IsTrue(own->state == *world.Flight(LEAD), L"the whole flight state, exactly");
      }
      motions += std::holds_alternative<Outpost::ShipMotionMessage>(*message) ? 1 : 0;
    }
    Assert::IsTrue(toldOwnShip);
    Assert::AreEqual(std::size_t{2}, motions, L"one engine state per flying ship");

    Outpost::ShipCommand full;
    full.wishes.throttle = 1.0f;
    const Outpost::Message input = Outpost::FlightInputMessage{1, LEAD, full};
    const Outpost::Route route = Outpost::RouteOf(Outpost::KindOf(input));
    Assert::IsTrue(transport.Send(Neuron::Peer::Client, route.delivery, route.stream, Outpost::Encode(input)));
    std::string error;
    Assert::IsTrue(Outpost::ReceiveCommands(transport, world, error), Wide(error).c_str());
    world.Tick();
    Assert::IsTrue(world.State().FindMotion(LEAD)->throttle == 1.0f, L"the command flew tick 1");

    Assert::IsTrue(observer.Publish(world, transport));
    bool toldAgain = false;
    while (const std::optional<Outpost::Message> message = NextMessage(transport, Neuron::Peer::Client))
    {
      toldAgain = toldAgain || std::holds_alternative<Outpost::OwnShipMessage>(*message);
    }
    Assert::IsFalse(toldAgain, L"a predictable tick is not told again");

    world.Tick(); // tick 2 had no command: the client's prediction and the server part
    Assert::IsTrue(observer.Publish(world, transport));
    while (const std::optional<Outpost::Message> message = NextMessage(transport, Neuron::Peer::Client))
    {
      toldAgain = toldAgain || std::holds_alternative<Outpost::OwnShipMessage>(*message);
    }
    Assert::IsTrue(toldAgain, L"a diverged tick is corrected");
  }

  TEST_METHOD(AServerMessageFromTheClientIsADefect)
  {
    Outpost::World world = LoadedWorld();
    Neuron::LoopbackTransport transport;
    const Outpost::Message tick = Outpost::TickMessage{5};
    Assert::IsTrue(transport.Send(Neuron::Peer::Client, Neuron::Delivery::Datagram, 0, Outpost::Encode(tick)));
    std::string error;
    Assert::IsFalse(Outpost::ReceiveCommands(transport, world, error));
    Assert::IsFalse(error.empty());
  }
};

} // namespace GameLogicTests
