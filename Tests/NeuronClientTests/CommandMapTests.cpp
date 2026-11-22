#include "CommandMap.h"

#include "CppUnitTest.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

constexpr std::string_view BINDINGS = R"(context name=Always priority=0
context name=Flying priority=10
context name=Tactical priority=10
command name=Thruster hold=true
command name=Cruise
command name=ThrottleUp hold=true repeat=true
command name=ResetView
command name=Quit
command name=ThrottleStepUp
bind command=Thruster context=Flying key=Tab
bind command=Cruise context=Flying key=C
bind command=ThrottleUp context=Flying key=W
bind command=ResetView context=Tactical key=Tab
bind command=Quit context=Always key=Escape
bind command=Cruise context=Flying key=Escape
bind command=ThrottleStepUp context=Flying key=WheelUp
)";

constexpr Neuron::InputKey TAB = 0x09;
constexpr Neuron::InputKey ESCAPE = 0x1B;

[[nodiscard]] Neuron::CommandMap Map()
{
  Neuron::CommandMap map;
  std::string error;
  Assert::IsTrue(Neuron::CommandMap::Parse(BINDINGS, "Bindings.txt", map, error), std::wstring(error.begin(), error.end()).c_str());
  return map;
}

[[nodiscard]] Neuron::CommandEvent Event(const Neuron::CommandMap& _map, std::string_view _command, Neuron::CommandPhase _phase)
{
  return Neuron::CommandEvent{_map.FindCommand(_command), _phase};
}

void AssertError(std::string_view _text, std::string_view _contains)
{
  Neuron::CommandMap map;
  std::string error;
  Assert::IsFalse(Neuron::CommandMap::Parse(_text, "Bindings.txt", map, error));
  Assert::IsTrue(error.find(_contains) != std::string::npos, std::wstring(error.begin(), error.end()).c_str());
}

} // namespace

TEST_CLASS(CommandMapTests)
{
public:
  TEST_METHOD(KeysAreNamedAsTheBindingsFileWritesThem)
  {
    Assert::AreEqual(Neuron::InputKey{'W'}, Neuron::KeyFromName("W"));
    Assert::AreEqual(Neuron::InputKey{'7'}, Neuron::KeyFromName("7"));
    Assert::AreEqual(Neuron::InputKey{0x7A}, Neuron::KeyFromName("F11"));
    Assert::AreEqual(TAB, Neuron::KeyFromName("Tab"));
    Assert::AreEqual(Neuron::KEY_WHEEL_DOWN, Neuron::KeyFromName("WheelDown"));
    Assert::AreEqual(Neuron::NO_KEY, Neuron::KeyFromName("w"), L"names are case-sensitive, as content names are");
    Assert::AreEqual(Neuron::NO_KEY, Neuron::KeyFromName("F25"));
    for (const std::string_view name : {"A", "Z", "0", "F1", "F24", "Escape", "Space", "MouseLeft", "WheelUp", "Backquote"})
    {
      Assert::IsTrue(Neuron::KeyName(Neuron::KeyFromName(name)) == name, L"names round-trip");
    }
  }

  TEST_METHOD(ABindingLooksUpItsCommandInALiveContext)
  {
    Neuron::CommandMap map = Map();
    map.KeyDown('C', false);
    Assert::IsTrue(map.TakeEvents().empty(), L"Flying is not live yet");
    map.KeyUp('C');
    map.SetContextActive(map.FindContext("Flying"), true);
    map.KeyDown('C', false);
    map.KeyUp('C');
    const std::vector<Neuron::CommandEvent> events = map.TakeEvents();
    Assert::AreEqual(std::size_t{1}, events.size(), L"a press command has no release");
    Assert::IsTrue(events[0] == Event(map, "Cruise", Neuron::CommandPhase::Pressed));
  }

  TEST_METHOD(AHoldCommandIsPressedAndReleasedAndHeldBetween)
  {
    Neuron::CommandMap map = Map();
    map.SetContextActive(map.FindContext("Flying"), true);
    map.KeyDown(TAB, false);
    Assert::IsTrue(map.IsHeld(map.FindCommand("Thruster")));
    map.KeyUp(TAB);
    Assert::IsFalse(map.IsHeld(map.FindCommand("Thruster")));
    const std::vector<Neuron::CommandEvent> events = map.TakeEvents();
    Assert::AreEqual(std::size_t{2}, events.size());
    Assert::IsTrue(events[1] == Event(map, "Thruster", Neuron::CommandPhase::Released));
  }

  TEST_METHOD(OnlyARepeatCommandTakesAutoRepeat)
  {
    Neuron::CommandMap map = Map();
    map.SetContextActive(map.FindContext("Flying"), true);
    map.KeyDown('W', false);
    map.KeyDown('W', true);
    map.KeyDown(TAB, false);
    map.KeyDown(TAB, true);
    const std::vector<Neuron::CommandEvent> events = map.TakeEvents();
    Assert::AreEqual(std::size_t{3}, events.size());
    Assert::IsTrue(events[1] == Event(map, "ThrottleUp", Neuron::CommandPhase::Repeated));
    Assert::IsTrue(events[2] == Event(map, "Thruster", Neuron::CommandPhase::Pressed), L"the thruster's repeat is ignored");
  }

  TEST_METHOD(TheHigherPriorityContextWinsAKeyBoundInBoth)
  {
    Neuron::CommandMap map = Map();
    map.SetContextActive(map.FindContext("Always"), true);
    map.KeyDown(ESCAPE, false);
    map.KeyUp(ESCAPE);
    map.SetContextActive(map.FindContext("Flying"), true);
    map.KeyDown(ESCAPE, false);
    map.KeyUp(ESCAPE);
    const std::vector<Neuron::CommandEvent> events = map.TakeEvents();
    Assert::AreEqual(std::size_t{2}, events.size());
    Assert::IsTrue(events[0] == Event(map, "Quit", Neuron::CommandPhase::Pressed), L"Always alone: Escape quits");
    Assert::IsTrue(events[1] == Event(map, "Cruise", Neuron::CommandPhase::Pressed), L"with Flying live, its binding wins");
  }

  TEST_METHOD(AKeyHeldAcrossAContextSwitchReleasesCleanly)
  {
    // Tab is the thruster in flight and resets the view in the tactical view. Held while the view
    // switches, its release must release the thruster and press nothing of the tactical view's.
    Neuron::CommandMap map = Map();
    map.SetContextActive(map.FindContext("Flying"), true);
    map.KeyDown(TAB, false);
    map.SetContextActive(map.FindContext("Flying"), false);
    map.SetContextActive(map.FindContext("Tactical"), true);
    map.KeyDown(TAB, true);
    map.KeyUp(TAB);
    const std::vector<Neuron::CommandEvent> events = map.TakeEvents();
    Assert::AreEqual(std::size_t{2}, events.size());
    Assert::IsTrue(events[0] == Event(map, "Thruster", Neuron::CommandPhase::Pressed));
    Assert::IsTrue(events[1] == Event(map, "Thruster", Neuron::CommandPhase::Released));
    Assert::IsFalse(map.IsHeld(map.FindCommand("Thruster")), L"the thruster cannot stick on");

    map.KeyDown(TAB, false);
    const std::vector<Neuron::CommandEvent> after = map.TakeEvents();
    Assert::AreEqual(std::size_t{1}, after.size());
    Assert::IsTrue(after[0] == Event(map, "ResetView", Neuron::CommandPhase::Pressed), L"the next press is the new context's");
  }

  TEST_METHOD(ReleasingEverythingReleasesHeldCommands)
  {
    Neuron::CommandMap map = Map();
    map.SetContextActive(map.FindContext("Flying"), true);
    map.KeyDown(TAB, false);
    map.KeyDown('W', false);
    static_cast<void>(map.TakeEvents());
    map.ReleaseAll();
    Assert::AreEqual(std::size_t{2}, map.TakeEvents().size());
    Assert::IsFalse(map.IsHeld(map.FindCommand("ThrottleUp")));
  }

  TEST_METHOD(AWheelNotchIsAPress)
  {
    Neuron::CommandMap map = Map();
    map.SetContextActive(map.FindContext("Flying"), true);
    map.Wheel(Neuron::KEY_WHEEL_UP);
    map.Wheel(Neuron::KEY_WHEEL_UP);
    const std::vector<Neuron::CommandEvent> events = map.TakeEvents();
    Assert::AreEqual(std::size_t{2}, events.size());
    Assert::IsTrue(events[1] == Event(map, "ThrottleStepUp", Neuron::CommandPhase::Pressed));
  }

  TEST_METHOD(BrokenBindingsNameTheLine)
  {
    AssertError("context name=A priority=0\ncommand name=X\nbind command=X context=A key=Nope\n", "line 3: unknown key 'Nope'");
    AssertError("context name=A priority=0\nbind command=X context=A key=W\n", "unknown command 'X'");
    AssertError("command name=X\nbind command=X context=B key=W\n", "unknown context 'B'");
    AssertError(
      "context name=A priority=0\ncommand name=X\ncommand name=Y\nbind command=X context=A key=W\nbind command=Y context=A key=W\n",
      "bound twice");
    AssertError("command name=X hold=maybe\n", "line 1");
    AssertError("key name=W\n", "unknown keyword 'key'");
  }

  TEST_METHOD(TheShippedBindingsParse)
  {
    Neuron::CommandMap map;
    std::string error;
    // The tests run from x64/Debug; the content is three levels up.
    bool loaded = false;
    for (const char* root : {"Assets/Content/Bindings.txt", "../Assets/Content/Bindings.txt", "../../Assets/Content/Bindings.txt",
                             "../../../Assets/Content/Bindings.txt"})
    {
      loaded = loaded || Neuron::CommandMap::Load(root, map, error);
    }
    Assert::IsTrue(loaded, std::wstring(error.begin(), error.end()).c_str());
    for (const std::string_view command : {"MouseFlight", "MouseFlightLock", "Thruster", "Cruise", "EngineKill", "Dock", "Launch", "Quit"})
    {
      Assert::IsTrue(map.FindCommand(command) != Neuron::NO_COMMAND, L"the game's commands are bound");
      Assert::IsTrue(map.KeyFor(map.FindCommand(command)) != Neuron::NO_KEY);
    }
  }
};

} // namespace NeuronClientTests
