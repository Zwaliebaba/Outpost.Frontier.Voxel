#include "Prediction.h"
#include "ShipArchetype.h"

#include "CppUnitTest.h"

#include <DirectXMath.h>

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameAppTests
{

namespace
{

constexpr std::uint32_t TICKS_PER_SECOND = 60;

constexpr std::string_view SHIPS = R"(engine name=LightEngine thrust=48000 drag=599 reverse=0.2 chargetime=5 cruisepower=20
powerplant name=LightPowerPlant capacity=1000 charge=100 thrustcapacity=1000 thrustcharge=100
thruster name=BasicThruster force=72000 drain=165
ship name=Fighter model=Fighter mass=100 inertia=2800,2800,1000 drag=1 torque=24000,24000,58000 angulardrag=15000,15000,35000 strafe=20000 strafedrain=2 maxbank=30 engine=LightEngine powerplant=LightPowerPlant thruster=BasicThruster
)";

[[nodiscard]] Outpost::ShipBody Body()
{
  Outpost::ShipContent content;
  std::string error;
  Assert::IsTrue(Outpost::ParseShipContent(SHIPS, "Ships.txt", content, error));
  Outpost::ShipBody body;
  body.object = 1;
  body.spec = Outpost::FlightSpecFor(content, *content.FindShip("Fighter"));
  body.radiusMetres = 20.0f;
  return body;
}

[[nodiscard]] Outpost::CollisionBox Wall()
{
  Outpost::CollisionBox wall;
  wall.object = 9;
  wall.center = Outpost::WorldPosition{0.0, 0.0, 302.0};
  wall.halfExtentsMetres = DirectX::XMFLOAT3{100.0f, 100.0f, 2.0f};
  return wall;
}

[[nodiscard]] Outpost::ShipCommand Command(std::uint64_t _tick)
{
  Outpost::ShipCommand command;
  command.wishes.throttle = 1.0f;
  command.wishes.steering = DirectX::XMFLOAT3{_tick < 40 ? 0.3f : 0.0f, _tick % 50 < 25 ? 0.5f : -0.5f, 0.1f};
  command.wishes.thruster = _tick >= 60 && _tick < 90;
  return command;
}

} // namespace

TEST_CLASS(PredictionTests)
{
public:
  TEST_METHOD(ThePredictionIsTheServersStepTickForTick)
  {
    // The server's half of the bargain, run here by hand: StepShip against the same bodies.
    const Outpost::ShipBody body = Body();
    const std::vector<Outpost::CollisionBox> bodies{Wall()};
    Outpost::FlightState server = Outpost::RestingFlightState(body.spec, Outpost::Pose{});
    Outpost::Prediction prediction;
    prediction.Begin(body, 0, server);
    prediction.SetBodies(bodies);
    bool contact = false;
    for (std::uint64_t tick = 1; tick <= 600; ++tick)
    {
      const bool touched = prediction.Step(Command(tick), TICKS_PER_SECOND).contact;
      contact = contact || touched;
      static_cast<void>(Outpost::StepShip(body, Command(tick), bodies, TICKS_PER_SECOND, server));
      Assert::IsTrue(prediction.State() == server, (L"tick " + std::to_wstring(tick)).c_str());
      Assert::AreEqual(tick, prediction.Tick());
    }
    Assert::IsTrue(contact || prediction.State().position.z < 300.0, L"the run meant something");
  }

  TEST_METHOD(AnAgreeingCorrectionChangesNothing)
  {
    const Outpost::ShipBody body = Body();
    Outpost::FlightState server = Outpost::RestingFlightState(body.spec, Outpost::Pose{});
    Outpost::Prediction prediction;
    prediction.Begin(body, 0, server);
    for (std::uint64_t tick = 1; tick <= 10; ++tick)
    {
      static_cast<void>(prediction.Step(Command(tick), TICKS_PER_SECOND));
    }
    static_cast<void>(Outpost::StepShip(body, Command(1), {}, TICKS_PER_SECOND, server));
    Assert::IsFalse(prediction.Rebase(1, server, TICKS_PER_SECOND), L"the server agrees about tick 1");
    Assert::AreEqual(0u, prediction.Corrections());
  }

  TEST_METHOD(ACorrectionFliesTheLaterTicksAgainFromIt)
  {
    const Outpost::ShipBody body = Body();
    Outpost::Prediction prediction;
    prediction.Begin(body, 0, Outpost::RestingFlightState(body.spec, Outpost::Pose{}));
    for (std::uint64_t tick = 1; tick <= 20; ++tick)
    {
      static_cast<void>(prediction.Step(Command(tick), TICKS_PER_SECOND));
    }
    // The server says tick 5 ended somewhere else: a knock from a ship the client could not see.
    Outpost::FlightState knocked = *prediction.StateAt(5);
    knocked.velocityMetresPerSecond.x += 40.0f;
    Assert::IsTrue(prediction.Rebase(5, knocked, TICKS_PER_SECOND));

    Outpost::FlightState expected = knocked;
    for (std::uint64_t tick = 6; tick <= 20; ++tick)
    {
      static_cast<void>(Outpost::StepShip(body, Command(tick), {}, TICKS_PER_SECOND, expected));
    }
    Assert::IsTrue(prediction.State() == expected, L"ticks 6 to 20 flown again on their own commands");
    Assert::AreEqual(std::uint64_t{20}, prediction.Tick());
    Assert::AreEqual(1u, prediction.Corrections());
  }

  TEST_METHOD(OtherObjectsMoveBetweenTheirLastTwoPoses)
  {
    Outpost::WorldSnapshot world;
    Outpost::WorldObject object;
    object.id = 4;
    object.pose.position = Outpost::WorldPosition{0.0, 0.0, 0.0};
    object.poseTick = 10;
    world.objects.push_back(object);
    Outpost::PoseInterpolation interpolation;
    interpolation.Update(world);
    Assert::AreEqual(0.0, interpolation.At(world.objects[0], 10.5).position.z, L"one pose: it stays there");

    world.objects[0].pose.position.z = 8.0;
    world.objects[0].poseTick = 11;
    ++world.posesRevision;
    interpolation.Update(world);
    Assert::AreEqual(2.0, interpolation.At(world.objects[0], 10.25).position.z, 1.0e-9, L"a quarter of the way through tick 11");
    Assert::AreEqual(8.0, interpolation.At(world.objects[0], 12.0).position.z, L"never past the last pose");
  }

  TEST_METHOD(APoseBlendKeepsDoublePrecisionFarOut)
  {
    // 100 km out, a millimetre's motion still shows: the blend is in double precision.
    Outpost::Pose from;
    from.position = Outpost::WorldPosition{100'000.0, 0.0, 0.0};
    Outpost::Pose to = from;
    to.position.x += 0.004;
    const Outpost::Pose half = Outpost::BlendPoses(from, to, 0.5);
    Assert::AreEqual(100'000.002, half.position.x, 1.0e-9);
  }
};

} // namespace GameAppTests
