#include "TestSupport.h"

#include "FlightModel.h"
#include "ShipArchetype.h"

#include <cmath>
#include <numbers>
#include <string>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

constexpr std::uint32_t TICKS_PER_SECOND = 60; // ADR-007; GameCore takes the rate from its caller

/// The light fighter as Assets/Content/Ships.txt authors it: the blueprint's worked example.
[[nodiscard]] Outpost::ShipFlightSpec LightFighter()
{
  Outpost::ShipContent content;
  std::string error;
  Assert::IsTrue(Outpost::LoadShipContent(AssetRoot() / "Content" / "Ships.txt", content, error),
                 std::wstring(error.begin(), error.end()).c_str());
  const Outpost::ShipArchetype* ship = content.FindShip("EscortLancer");
  Assert::IsNotNull(ship);
  return Outpost::FlightSpecFor(content, *ship);
}

[[nodiscard]] Outpost::FlightState AtRest(const Outpost::ShipFlightSpec& _spec)
{
  return Outpost::RestingFlightState(_spec, Outpost::Pose{});
}

void Fly(const Outpost::ShipFlightSpec& _spec, const Outpost::FlightControls& _controls, float _seconds, Outpost::FlightState& _state)
{
  const auto ticks = static_cast<int>(std::lround(_seconds * static_cast<float>(TICKS_PER_SECOND)));
  for (int i = 0; i < ticks; ++i)
  {
    Outpost::StepFlight(_spec, _controls, TICKS_PER_SECOND, _state);
  }
}

[[nodiscard]] Outpost::FlightControls Throttle(float _throttle)
{
  Outpost::FlightControls controls;
  controls.throttle = _throttle;
  return controls;
}

/// Flies at full throttle until the speed has settled at the top speed.
[[nodiscard]] Outpost::FlightState AtTopSpeed(const Outpost::ShipFlightSpec& _spec)
{
  Outpost::FlightState state = AtRest(_spec);
  Fly(_spec, Throttle(1.0f), 3.0f, state);
  return state;
}

[[nodiscard]] double DistanceFromOrigin(const Outpost::FlightState& _state)
{
  const Outpost::UniversePosition& p = _state.position;
  return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
}

} // namespace

TEST_CLASS(FlightModelTests)
{
public:
  TEST_METHOD(TheContentIsTheBlueprintsLightFighter)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Assert::AreEqual(100.0f, spec.massKilograms);
    Assert::AreEqual(48000.0f, spec.engineThrustNewtons);
    Assert::AreEqual(599.0f, spec.engineDragKilogramsPerSecond);
    Assert::AreEqual(72000.0f, spec.thrusterForceNewtons);
    Assert::AreEqual(80.0f, spec.TopSpeedMetresPerSecond());
  }

  TEST_METHOD(TopSpeedIsThrustOverDrag)
  {
    const Outpost::FlightState state = AtTopSpeed(LightFighter());
    Assert::AreEqual(80.0f, state.SpeedMetresPerSecond(), 0.01f);
    Assert::AreEqual(80.0f, state.velocityMetresPerSecond.z, 0.01f, L"along the nose, +Z");
  }

  TEST_METHOD(NinetyFivePercentOfTopSpeedInHalfASecond)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Fly(spec, Throttle(1.0f), 0.4f, state);
    Assert::IsTrue(state.SpeedMetresPerSecond() < 0.95f * 80.0f, L"not yet at 0.4 s");
    Fly(spec, Throttle(1.0f), 0.1f, state);
    Assert::IsTrue(state.SpeedMetresPerSecond() >= 0.95f * 80.0f, L"by 0.5 s: the time constant is 100 / 600 s");
  }

  TEST_METHOD(StopsInThirteenMetresFromTopSpeed)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtTopSpeed(spec);
    const double before = DistanceFromOrigin(state);
    Fly(spec, Throttle(0.0f), 5.0f, state);
    const double stopping = DistanceFromOrigin(state) - before;
    Assert::AreEqual(80.0 * 100.0 / 600.0, stopping, 0.05, L"v m / D");
    Assert::IsTrue(state.SpeedMetresPerSecond() < 0.01f);
  }

  TEST_METHOD(ReverseIsAFifthOfThrust)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Fly(spec, Throttle(-1.0f), 3.0f, state);
    Assert::AreEqual(-16.0f, state.velocityMetresPerSecond.z, 0.01f, L"backwards, -Z");
  }

  TEST_METHOD(TheThrusterReachesTwoHundred)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls = Throttle(1.0f);
    controls.thruster = true;
    Fly(spec, controls, 3.0f, state);
    Assert::IsTrue(state.thrusterActive);
    Assert::AreEqual(200.0f, state.SpeedMetresPerSecond(), 0.05f);
  }

  TEST_METHOD(TheThrusterPoolLastsAboutFifteenSeconds)
  {
    // 165 a second drained against 100 recharged: 1,000 lasts 1,000 / 65 s.
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls = Throttle(1.0f);
    controls.thruster = true;
    int ticks = 0;
    do
    {
      Outpost::StepFlight(spec, controls, TICKS_PER_SECOND, state);
      ++ticks;
    } while (state.thrusterActive && ticks < 60 * TICKS_PER_SECOND);
    const float seconds = static_cast<float>(ticks) / TICKS_PER_SECOND;
    Assert::AreEqual(1000.0f / 65.0f, seconds, 0.1f);
  }

  TEST_METHOD(StrafeReachesThirtyThree)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls;
    controls.strafe = Outpost::StrafeDirection::Right;
    Fly(spec, controls, 3.0f, state);
    Assert::AreEqual(20000.0f / 600.0f, state.velocityMetresPerSecond.x, 0.01f, L"right is +X");
    Assert::IsTrue(state.thrustPool > spec.thrustCapacity - 0.1f, L"2 a second against 100 recharged: strafing never empties it");
  }

  TEST_METHOD(PitchAndYawTurnAtNinetyTwoDegreesASecond)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls;
    controls.steering = DirectX::XMFLOAT3{1.0f, 1.0f, 0.0f};
    Fly(spec, controls, 0.2f, state);
    // Near one response time, 2,800 / 15,000 s: the rate relaxes towards 1.6 rad/s with that time constant.
    Assert::AreEqual(1.6f * (1.0f - std::exp(-0.2f * 15000.0f / 2800.0f)), state.angularVelocityRadiansPerSecond.x, 0.001f);
    Fly(spec, controls, 2.0f, state);
    Assert::AreEqual(1.6f, state.angularVelocityRadiansPerSecond.x, 0.001f, L"24,000 / 15,000 rad/s");
    Assert::AreEqual(1.6f, state.angularVelocityRadiansPerSecond.y, 0.001f);
    Assert::AreEqual(92.0f, 1.6f * 180.0f / std::numbers::pi_v<float>, 0.5f);
  }

  TEST_METHOD(RollIsFasterAndQuicker)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls;
    controls.steering = DirectX::XMFLOAT3{0.0f, 0.0f, 1.0f};
    Fly(spec, controls, 0.25f, state);
    Assert::AreEqual(58000.0f / 35000.0f, state.angularVelocityRadiansPerSecond.z, 0.001f, L"settled within a quarter second");
  }

  TEST_METHOD(TurningTurnsTheNose)
  {
    // A quarter turn of yaw at the steady rate takes (pi / 2) / 1.6 s, plus the spin-up lag.
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls;
    controls.steering = DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f};
    Fly(spec, controls, std::numbers::pi_v<float> / 2.0f / 1.6f + 2800.0f / 15000.0f, state);
    DirectX::XMFLOAT3 nose{};
    DirectX::XMStoreFloat3(&nose, Outpost::NoseDirection(state));
    Assert::AreEqual(0.0f, nose.z, 0.02f, L"a quarter turn from +Z");
    Assert::AreEqual(1.0f, nose.x, 0.02f, L"a positive yaw turns the nose right, to +X");
  }

  TEST_METHOD(CruiseChargesForFiveSecondsThenRampsToThreeHundred)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtTopSpeed(spec);
    Outpost::FlightControls controls = Throttle(1.0f);
    controls.cruise = true;
    Fly(spec, controls, 4.9f, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Charging);
    Assert::AreEqual(80.0f, state.SpeedMetresPerSecond(), 0.5f, L"charging is normal thrust");
    Fly(spec, controls, 0.1f, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Cruising, L"after 5 s");
    Assert::AreEqual(48000.0f / 603.0f / 300.0f, state.cruiseRamp, 0.01f, L"the ramp starts at normal top speed");
    Fly(spec, controls, 3.0f, state);
    Assert::IsTrue(state.SpeedMetresPerSecond() < 290.0f, L"still ramping 3 s in");
    Fly(spec, controls, 0.7f + 1.0f, state);
    Assert::AreEqual(300.0f, state.SpeedMetresPerSecond(), 3.0f, L"about 3.7 s after charging, and the 0.17 s lag");
    Fly(spec, controls, 5.0f, state);
    Assert::AreEqual(300.0f, state.SpeedMetresPerSecond(), 0.05f, L"every ship cruises at 300 m/s");
  }

  TEST_METHOD(CruiseDrawsFromThePowerPool)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    state.powerPool = 500.0f;
    Outpost::FlightControls controls = Throttle(1.0f);
    controls.cruise = true;
    Fly(spec, controls, 1.0f, state);
    Assert::AreEqual(500.0f + 100.0f - 20.0f, state.powerPool, 0.5f, L"charge 100/s against 20/s");
  }

  TEST_METHOD(ThePrecedenceOfCruise)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls = Throttle(1.0f);
    controls.cruise = true;
    controls.thruster = true;
    Fly(spec, controls, 0.5f, state);
    Assert::IsFalse(state.thrusterActive, L"the thruster is refused in cruise");

    controls.throttle = 0.0f;
    Fly(spec, controls, 1.0f / TICKS_PER_SECOND, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Off, L"a zero throttle disengages");

    controls.throttle = 1.0f;
    Fly(spec, controls, 0.5f, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Charging);
    controls.engineOn = false;
    Fly(spec, controls, 1.0f / TICKS_PER_SECOND, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Off, L"engine kill drops cruise");
  }

  TEST_METHOD(ADisruptionAddsItsCooldownToTheCharge)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls = Throttle(1.0f);
    controls.cruise = true;
    Fly(spec, controls, 6.0f, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Cruising);
    Outpost::DisruptCruise(TICKS_PER_SECOND, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Cooldown);

    Fly(spec, controls, 1.0f, state); // engaging at once charges from the cooldown's negative timer
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Charging);
    Fly(spec, controls, 4.0f + 5.0f - 0.1f, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Charging, L"the rest of the cooldown, then the full charge");
    Fly(spec, controls, 0.2f, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Cruising);
  }

  TEST_METHOD(ACooldownLeftAloneEndsInOff)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls = Throttle(1.0f);
    controls.cruise = true;
    Fly(spec, controls, 1.0f, state);
    Outpost::DisruptCruise(TICKS_PER_SECOND, state);
    controls.cruise = false;
    Fly(spec, controls, 4.9f, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Cooldown);
    Fly(spec, controls, 0.2f, state);
    Assert::IsTrue(state.cruise == Outpost::CruiseState::Off);
  }

  TEST_METHOD(EngineKillCoastsAndSpeedHalvesInSixtyNineSeconds)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtTopSpeed(spec);
    Outpost::FlightControls controls;
    controls.engineOn = false;
    Fly(spec, controls, 69.3f, state);
    Assert::AreEqual(40.0f, state.SpeedMetresPerSecond(), 0.1f, L"k = 1 / 100: ln 2 / k = 69.3 s");
  }

  TEST_METHOD(SteeringKeepsFullAuthorityWithTheEngineKilled)
  {
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState state = AtTopSpeed(spec);
    Outpost::FlightControls controls;
    controls.engineOn = false;
    controls.steering = DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f};
    Fly(spec, controls, 2.0f, state);
    Assert::AreEqual(1.6f, state.angularVelocityRadiansPerSecond.y, 0.001f);
    Assert::IsTrue(state.SpeedMetresPerSecond() > 75.0f, L"the velocity stays while the nose turns");
  }

  TEST_METHOD(TheGlobalLimitsHold)
  {
    Outpost::ShipFlightSpec spec;
    spec.massKilograms = 1.0f;
    spec.engineThrustNewtons = 1.0e7f;
    spec.engineDragKilogramsPerSecond = 1.0f;
    spec.steeringTorqueNewtonMetres = DirectX::XMFLOAT3{0.0f, 1.0e6f, 0.0f};
    spec.angularDragNewtonMetreSeconds = DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f};
    Outpost::FlightState state = AtRest(spec);
    Outpost::FlightControls controls = Throttle(1.0f);
    controls.steering = DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 10; ++i)
    {
      const DirectX::XMFLOAT4 before = state.orientation;
      Outpost::StepFlight(spec, controls, TICKS_PER_SECOND, state);
      const float dot = std::abs(before.x * state.orientation.x + before.y * state.orientation.y + before.z * state.orientation.z +
                                 before.w * state.orientation.w);
      Assert::IsTrue(2.0f * std::acos(std::min(1.0f, dot)) <= Outpost::MAX_ROTATION_RADIANS_PER_TICK + 1.0e-3f, L"0.94 rad a tick");
    }
    Assert::IsTrue(state.SpeedMetresPerSecond() <= Outpost::MAX_SPEED_METRES_PER_SECOND + 1.0e-2f, L"1,000 m/s");
  }

  TEST_METHOD(TheSameInputGivesTheSameFlight)
  {
    // Both halves run this code; in one build, the same inputs must give the same bits (plan task 1.7).
    const Outpost::ShipFlightSpec spec = LightFighter();
    Outpost::FlightState a = AtRest(spec);
    Outpost::FlightState b = AtRest(spec);
    Outpost::FlightControls controls = Throttle(0.7f);
    controls.steering = DirectX::XMFLOAT3{0.3f, -0.6f, 0.1f};
    Fly(spec, controls, 5.0f, a);
    Fly(spec, controls, 5.0f, b);
    Assert::IsTrue(a.position == b.position);
    Assert::AreEqual(a.orientation.w, b.orientation.w);
  }
};

} // namespace GameCoreTests
