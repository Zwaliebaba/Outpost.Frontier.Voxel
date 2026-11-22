#include "TestSupport.h"

#include "FlightModel.h"
#include "Manoeuvre.h"
#include "ShipArchetype.h"

#include <DirectXMath.h>

#include <cmath>
#include <numbers>
#include <string>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

constexpr std::uint32_t TICKS_PER_SECOND = 60;
constexpr float STAND_OFF_METRES = 50.0f;

[[nodiscard]] Outpost::ShipFlightSpec Fighter()
{
  Outpost::ShipContent content;
  std::string error;
  Assert::IsTrue(Outpost::LoadShipContent(AssetRoot() / "Content" / "Ships.txt", content, error),
                 std::wstring(error.begin(), error.end()).c_str());
  return Outpost::FlightSpecFor(content, *content.FindShip("EscortTalon"));
}

[[nodiscard]] Outpost::FlightState StartAt(const Outpost::ShipFlightSpec& _spec, Outpost::WorldPosition _position, float _yawDegrees,
                                           float _pitchDegrees)
{
  Outpost::Pose pose;
  pose.position = _position;
  const float toRadians = std::numbers::pi_v<float> / 180.0f;
  DirectX::XMStoreFloat4(&pose.orientation,
                         DirectX::XMQuaternionRotationRollPitchYaw(_pitchDegrees * toRadians, _yawDegrees * toRadians, 0.0f));
  return Outpost::RestingFlightState(_spec, pose);
}

[[nodiscard]] double Distance(const Outpost::WorldPosition& _a, const Outpost::WorldPosition& _b)
{
  const double dx = _a.x - _b.x;
  const double dy = _a.y - _b.y;
  const double dz = _a.z - _b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Runs a manoeuvre until it reports arrival or the time runs out; the seconds it took, or -1.
float RunUntilArrived(const Outpost::Manoeuvre& _manoeuvre, const Outpost::ShipFlightSpec& _spec, float _limitSeconds,
                      Outpost::FlightState& _state)
{
  const int limit = static_cast<int>(_limitSeconds * TICKS_PER_SECOND);
  for (int tick = 0; tick < limit; ++tick)
  {
    const Outpost::ManoeuvreOutput output = Outpost::RunManoeuvre(_manoeuvre, Outpost::FlightControls{}, _spec, _state);
    if (output.arrived)
    {
      return static_cast<float>(tick) / TICKS_PER_SECOND;
    }
    Outpost::StepFlight(_spec, output.controls, TICKS_PER_SECOND, _state);
  }
  return -1.0f;
}

} // namespace

TEST_CLASS(ManoeuvreTests)
{
public:
  TEST_METHOD(FreeFlightPassesInputThroughUnchanged)
  {
    const Outpost::ShipFlightSpec spec = Fighter();
    const Outpost::FlightState state = StartAt(spec, {}, 0.0f, 0.0f);
    Outpost::FlightControls input;
    input.throttle = 0.6f;
    input.steering = DirectX::XMFLOAT3{-0.4f, 0.25f, 0.1f};
    input.strafe = Outpost::StrafeDirection::Up;
    input.thruster = true;
    input.cruise = true;
    const Outpost::ManoeuvreOutput output = Outpost::RunManoeuvre(Outpost::Manoeuvre::FreeFlight(), input, spec, state);
    Assert::IsTrue(output.controls == input);
    Assert::IsFalse(output.arrived);
  }

  TEST_METHOD(ReverseBeatsThrottle)
  {
    Outpost::FlightControls wishes;
    wishes.throttle = 1.0f;
    wishes.cruise = true;
    wishes.reverse = true;
    const Outpost::FlightControls controls = Outpost::Arbitrate(wishes);
    Assert::AreEqual(-1.0f, controls.throttle);
    Assert::IsFalse(controls.cruise);
  }

  TEST_METHOD(AKilledEngineBeatsCruise)
  {
    Outpost::FlightControls wishes;
    wishes.throttle = 1.0f;
    wishes.cruise = true;
    wishes.engineOn = false;
    const Outpost::FlightControls controls = Outpost::Arbitrate(wishes);
    Assert::IsFalse(controls.cruise);
    Assert::AreEqual(1.0f, controls.throttle, L"the throttle is kept for when the engine returns");
  }

  TEST_METHOD(GoToArrivesWithinItsStandOffFromSeveralStarts)
  {
    const Outpost::ShipFlightSpec spec = Fighter();
    const Outpost::WorldPosition target{1.0e6, -2.0e5, 3.0e5}; // far from the origin: offsets are taken in double
    struct Start
    {
      Outpost::WorldPosition offset;
      float yawDegrees;
      float pitchDegrees;
      const wchar_t* what;
    };
    const Start starts[] = {
      {{0.0, 0.0, 600.0}, 0.0f, 0.0f, L"facing it"},        {{0.0, 0.0, 600.0}, 180.0f, 0.0f, L"facing away"},
      {{-400.0, 0.0, 0.0}, 0.0f, 0.0f, L"side on"},         {{300.0, 400.0, -200.0}, 60.0f, 45.0f, L"above, pitched"},
      {{2500.0, -1500.0, 1800.0}, -120.0f, -30.0f, L"far"}, {{0.0, 0.0, 70.0}, 90.0f, 0.0f, L"just outside the stand-off"},
    };
    for (const Start& start : starts)
    {
      const Outpost::WorldPosition from{target.x + start.offset.x, target.y + start.offset.y, target.z + start.offset.z};
      Outpost::FlightState state = StartAt(spec, from, start.yawDegrees, start.pitchDegrees);
      const float seconds = RunUntilArrived(Outpost::Manoeuvre::GoTo(target, STAND_OFF_METRES), spec, 120.0f, state);
      Assert::IsTrue(seconds >= 0.0f, start.what);
      const double distance = Distance(state.position, target);
      Assert::IsTrue(distance <= STAND_OFF_METRES + Outpost::ARRIVAL_TOLERANCE_METRES, start.what);
      Assert::IsTrue(distance >= STAND_OFF_METRES - Outpost::ARRIVAL_TOLERANCE_METRES, start.what);
    }
  }

  TEST_METHOD(GoToTakesAboutTheTimeTopSpeedAllows)
  {
    // 2 km head-on at 80 m/s is 25 s; turning and the approach add a little.
    const Outpost::ShipFlightSpec spec = Fighter();
    Outpost::FlightState state = StartAt(spec, {0.0, 0.0, 2050.0}, 0.0f, 0.0f);
    const float seconds = RunUntilArrived(Outpost::Manoeuvre::GoTo({}, STAND_OFF_METRES), spec, 120.0f, state);
    Assert::IsTrue(seconds > 25.0f && seconds < 28.0f);
  }

  TEST_METHOD(HoldFliesBackToItsAnchor)
  {
    const Outpost::ShipFlightSpec spec = Fighter();
    const Outpost::WorldPosition anchor{100.0, 20.0, -50.0};
    Outpost::FlightState state = StartAt(spec, {300.0, 20.0, -50.0}, 0.0f, 0.0f);
    const Outpost::Manoeuvre hold = Outpost::Manoeuvre::Hold(anchor);
    Assert::IsFalse(Outpost::RunManoeuvre(hold, {}, spec, state).arrived);
    Assert::IsTrue(RunUntilArrived(hold, spec, 60.0f, state) >= 0.0f);
    Assert::IsTrue(Distance(state.position, anchor) <= Outpost::HOLD_TOLERANCE_METRES);
  }

  TEST_METHOD(AManoeuvreIsPure)
  {
    const Outpost::ShipFlightSpec spec = Fighter();
    const Outpost::FlightState state = StartAt(spec, {10.0, 0.0, 900.0}, 30.0f, 10.0f);
    const Outpost::Manoeuvre goTo = Outpost::Manoeuvre::GoTo({}, STAND_OFF_METRES);
    Assert::IsTrue(Outpost::RunManoeuvre(goTo, {}, spec, state).controls == Outpost::RunManoeuvre(goTo, {}, spec, state).controls);
  }
};

} // namespace GameCoreTests
