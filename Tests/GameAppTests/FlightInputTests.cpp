#include "CommandMap.h"
#include "FlightInput.h"

#include "CppUnitTest.h"

#include <DirectXMath.h>

#include <cmath>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameAppTests
{

namespace
{

constexpr std::uint32_t TICKS_PER_SECOND = 60;
constexpr float WIDTH = 1920.0f;
constexpr float HEIGHT = 1080.0f;
constexpr DirectX::XMFLOAT3 UP{0.0f, 1.0f, 0.0f};

constexpr std::string_view BINDINGS = R"(context name=Flying priority=10
command name=ThrottleUp hold=true
command name=ThrottleDown hold=true
command name=ThrottleStepUp
command name=Cruise
command name=MouseFlight hold=true
command name=MouseFlightLock
command name=RollLeft hold=true
command name=Cancel
bind command=ThrottleUp context=Flying key=W
bind command=ThrottleDown context=Flying key=S
bind command=ThrottleStepUp context=Flying key=WheelUp
bind command=Cruise context=Flying key=C
bind command=MouseFlight context=Flying key=MouseLeft
bind command=MouseFlightLock context=Flying key=Space
bind command=RollLeft context=Flying key=Q
bind command=Cancel context=Flying key=Escape
)";

struct Rig
{
  Neuron::CommandMap commands;
  Outpost::FlightInput input;
  Outpost::FlightState state;

  Rig()
  {
    std::string error;
    Assert::IsTrue(Neuron::CommandMap::Parse(BINDINGS, "Bindings.txt", commands, error));
    commands.SetContextActive(commands.FindContext("Flying"), true);
    input.Bind(commands);
    input.SetCursor(WIDTH * 0.5f, HEIGHT * 0.5f, WIDTH, HEIGHT);
  }

  void Key(Neuron::InputKey _key, bool _down)
  {
    if (_down)
    {
      commands.KeyDown(_key, false);
    }
    else
    {
      commands.KeyUp(_key);
    }
    for (const Neuron::CommandEvent& event : commands.TakeEvents())
    {
      input.OnCommand(event);
    }
  }

  Outpost::ShipCommand Tick(float _maxBank = 30.0f)
  {
    return input.Sample(commands, state, UP, _maxBank, TICKS_PER_SECOND);
  }
};

} // namespace

TEST_CLASS(FlightInputTests)
{
public:
  TEST_METHOD(TheCursorDemandsByItsOffsetOverHalfTheScreen)
  {
    const DirectX::XMFLOAT2 center = Outpost::FlightInput::CursorDemand(960.0f, 540.0f, WIDTH, HEIGHT);
    Assert::AreEqual(0.0f, center.x);
    Assert::AreEqual(0.0f, center.y);
    const DirectX::XMFLOAT2 edge = Outpost::FlightInput::CursorDemand(1920.0f, 540.0f, WIDTH, HEIGHT);
    Assert::AreEqual(1.0f, edge.x, L"the right edge is full right");
    const DirectX::XMFLOAT2 halfway = Outpost::FlightInput::CursorDemand(960.0f, 270.0f, WIDTH, HEIGHT);
    Assert::AreEqual(-0.5f, halfway.y, 1.0e-6f, L"a quarter of the height up is half nose-up");
    const DirectX::XMFLOAT2 corner = Outpost::FlightInput::CursorDemand(-50.0f, 2000.0f, WIDTH, HEIGHT);
    Assert::AreEqual(-1.0f, corner.x, L"clamped beyond the corner");
    Assert::AreEqual(1.0f, corner.y);
  }

  TEST_METHOD(AClickDoesNotSteerButAHoldDoes)
  {
    Rig rig;
    rig.input.SetCursor(1440.0f, 540.0f, WIDTH, HEIGHT); // half right, inside no click radius of the press
    rig.Key(Neuron::KEY_MOUSE_LEFT, true);
    for (int tick = 1; tick < 15; ++tick)
    {
      Assert::AreEqual(0.0f, rig.Tick().wishes.steering.y, L"under a quarter-second held, the cursor does not steer");
    }
    Assert::AreEqual(0.5f, rig.Tick().wishes.steering.y, 1.0e-6f, L"the fifteenth tick arms it: half right is half yaw");
    rig.Key(Neuron::KEY_MOUSE_LEFT, false);
    Assert::AreEqual(0.0f, rig.Tick().wishes.steering.y, L"released, it stops");
  }

  TEST_METHOD(LeavingTheClickRadiusArmsAtOnce)
  {
    Rig rig;
    rig.Key(Neuron::KEY_MOUSE_LEFT, true);
    rig.input.SetCursor(960.0f + WIDTH / 16.0f + 1.0f, 540.0f, WIDTH, HEIGHT);
    Assert::IsTrue(rig.Tick().wishes.steering.y > 0.0f, L"a drag beyond a sixteenth of the width steers on the next tick");
  }

  TEST_METHOD(TheLockMakesTheMouseAStick)
  {
    Rig rig;
    rig.input.SetCursor(960.0f, 810.0f, WIDTH, HEIGHT);
    rig.Key(' ', true);
    Assert::IsTrue(rig.input.MouseLocked());
    Assert::AreEqual(0.5f, rig.Tick().wishes.steering.x, 1.0e-6f, L"below the centre turns the nose down");
    rig.input.SetMouseEnabled(false);
    Assert::AreEqual(0.0f, rig.Tick().wishes.steering.x, L"not while the tactical view has the cursor");
  }

  TEST_METHOD(TheThrottleMovesAtItsRateAndTheWheelSteps)
  {
    Rig rig;
    rig.Key('W', true);
    for (int tick = 0; tick < 30; ++tick)
    {
      static_cast<void>(rig.Tick());
    }
    Assert::AreEqual(0.5f, rig.input.Throttle(), 1.0e-4f, L"half a second at 1 a second");
    for (int tick = 0; tick < 60; ++tick)
    {
      static_cast<void>(rig.Tick());
    }
    Assert::AreEqual(1.0f, rig.input.Throttle(), L"capped at full");
    rig.Key('W', false);
    rig.Key('S', true);
    for (int tick = 0; tick < 120; ++tick)
    {
      static_cast<void>(rig.Tick());
    }
    Assert::AreEqual(0.0f, rig.input.Throttle());
    rig.Key('S', false);
    rig.commands.Wheel(Neuron::KEY_WHEEL_UP);
    for (const Neuron::CommandEvent& event : rig.commands.TakeEvents())
    {
      rig.input.OnCommand(event);
    }
    Assert::AreEqual(0.2f, rig.input.Throttle(), 1.0e-6f);
  }

  TEST_METHOD(CruiseSetsFullThrottleAndThrottlingDownDropsIt)
  {
    Rig rig;
    rig.Key('C', true);
    Assert::IsTrue(rig.input.CruiseWish());
    Assert::AreEqual(1.0f, rig.input.Throttle());
    Assert::IsTrue(rig.Tick().wishes.cruise);
    rig.Key('S', true);
    Assert::IsFalse(rig.Tick().wishes.cruise, L"the throttle-down key drops cruise");
  }

  TEST_METHOD(ABankIntoATurnFollowsYawAndThrottleWithAFloor)
  {
    Assert::AreEqual(-30.0f, Outpost::FlightInput::BankTarget(30.0f, 1.0f, 1.0f), L"a full right turn: the right wing 30 degrees down");
    Assert::AreEqual(15.0f, Outpost::FlightInput::BankTarget(30.0f, -0.5f, 1.0f));
    Assert::AreEqual(-12.0f, Outpost::FlightInput::BankTarget(30.0f, 1.0f, 0.0f), 1.0e-5f, L"0.4 of it at no throttle");
    Assert::AreEqual(-18.0f, Outpost::FlightInput::BankTarget(30.0f, 1.0f, 0.6f), 1.0e-5f);

    DirectX::XMFLOAT4 rolled{};
    DirectX::XMStoreFloat4(&rolled, DirectX::XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, DirectX::XMConvertToRadians(20.0f)));
    Assert::AreEqual(20.0f, Outpost::FlightInput::BankOf(rolled, UP), 1.0e-3f, L"a roll that lifts the right wing is a positive bank");
  }

  TEST_METHOD(BankingRollsTowardsTheTargetUnlessARollKeyIsHeld)
  {
    Rig rig;
    rig.input.SetCursor(1920.0f, 540.0f, WIDTH, HEIGHT);
    rig.Key(' ', true);
    // Level, full right yaw, no throttle: the target is -12 degrees, so roll right (negative).
    Assert::AreEqual(-12.0f / Outpost::FlightInput::BANK_GAIN_DEGREES, rig.Tick(30.0f).wishes.steering.z, 1.0e-4f);
    rig.Key('Q', true);
    Assert::AreEqual(1.0f, rig.Tick(30.0f).wishes.steering.z, L"the roll key overrides the assist");
  }

  TEST_METHOD(AnOrderIsFlownUntilThePlayerFliesAgain)
  {
    Rig rig;
    rig.input.GoTo(Outpost::UniversePosition{100.0, 0.0, 500.0}, 10.0f);
    Outpost::ShipCommand command = rig.Tick();
    Assert::IsTrue(command.manoeuvre.kind == Outpost::ManoeuvreKind::GoTo);
    Assert::AreEqual(10.0f, command.manoeuvre.standOffMetres);
    rig.Key('W', true);
    command = rig.Tick();
    Assert::IsTrue(command.manoeuvre.kind == Outpost::ManoeuvreKind::FreeFlight, L"the throttle takes the ship back");

    rig.Key('W', false);
    rig.input.GoTo(Outpost::UniversePosition{}, 0.0f);
    rig.Key(0x1B, true);
    Assert::IsTrue(rig.Tick().manoeuvre.kind == Outpost::ManoeuvreKind::FreeFlight, L"and so does Cancel");
  }
};

} // namespace GameAppTests
