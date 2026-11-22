#include "DockedView.h"
#include "FileSys.h"
#include "FontAtlas.h"
#include "HudBatch.h"
#include "InputScript.h"
#include "TacticalHud.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameAppTests
{

namespace
{

[[nodiscard]] Outpost::InputScript Parsed(std::string_view _text)
{
  Outpost::InputScript script;
  std::string error;
  Assert::IsTrue(Outpost::InputScript::Parse(_text, script, error), std::wstring(error.begin(), error.end()).c_str());
  return script;
}

[[nodiscard]] std::string ParseError(std::string_view _text)
{
  Outpost::InputScript script;
  std::string error;
  Assert::IsFalse(Outpost::InputScript::Parse(_text, script, error));
  return error;
}

int g_moduleAnchor = 0;

/// A smoke script stored in Design/Measurements, found by walking up from the test DLL as the assets
/// are.
[[nodiscard]] Outpost::InputScript StoredScript(const char* _name)
{
  std::filesystem::path directory = Neuron::ModuleDirectory(&g_moduleAnchor);
  std::filesystem::path file;
  for (int parents = 0; parents <= 4 && file.empty(); ++parents)
  {
    const std::filesystem::path candidate = directory / "Design" / "Measurements" / _name;
    std::error_code ignored;
    if (std::filesystem::is_regular_file(candidate, ignored))
    {
      file = candidate;
    }
    directory = directory.parent_path();
  }
  Assert::IsFalse(file.empty(), L"the script is found in Design/Measurements above the test DLL");
  Outpost::InputScript script;
  std::string error;
  Assert::IsTrue(Outpost::InputScript::Load(file, script, error), std::wstring(error.begin(), error.end()).c_str());
  return script;
}

} // namespace

TEST_CLASS(InputScriptTests)
{
public:
  TEST_METHOD(ADragPressesMovesEachTickAndReleases)
  {
    const Outpost::InputScript script = Parsed("drag tick=10 button=right from=100,200 to=140,220 ticks=4\n");
    const auto& events = script.Events();
    Assert::AreEqual(std::size_t{6}, events.size(), L"press, four moves, release");
    Assert::IsTrue(events.front().kind == Outpost::InputEvent::Kind::PointerDown);
    Assert::IsTrue(events.front().button == Outpost::PointerButton::Right);
    Assert::AreEqual(std::uint64_t{10}, events.front().tick);
    Assert::AreEqual(100.0f, events.front().xPixels);
    Assert::IsTrue(events[2].kind == Outpost::InputEvent::Kind::PointerMove);
    Assert::AreEqual(std::uint64_t{12}, events[2].tick);
    Assert::AreEqual(120.0f, events[2].xPixels, L"halfway at the second of four ticks");
    Assert::AreEqual(210.0f, events[2].yPixels);
    Assert::IsTrue(events.back().kind == Outpost::InputEvent::Kind::PointerUp);
    Assert::AreEqual(std::uint64_t{14}, events.back().tick);
    Assert::AreEqual(140.0f, events.back().xPixels);
    Assert::AreEqual(std::uint64_t{14}, script.LastTick());
  }

  TEST_METHOD(AClickPressesAndReleasesInOneTick)
  {
    const Outpost::InputScript script = Parsed("click tick=3 at=50,60\n");
    const auto events = script.EventsAt(3);
    Assert::AreEqual(std::size_t{2}, events.size());
    Assert::IsTrue(events[0].kind == Outpost::InputEvent::Kind::PointerDown);
    Assert::IsTrue(events[1].kind == Outpost::InputEvent::Kind::PointerUp);
    Assert::IsTrue(events[0].button == Outpost::PointerButton::Left, L"left by default");
    Assert::AreEqual(60.0f, events[1].yPixels);
  }

  TEST_METHOD(EventsAreInTickOrderWithFileOrderWithinATick)
  {
    const Outpost::InputScript script = Parsed("wheel tick=20 steps=-2\nhelp tick=5\nstatus tick=20\nreset tick=20\n");
    Assert::IsTrue(script.EventsAt(5)[0].kind == Outpost::InputEvent::Kind::ToggleHelp);
    const auto atTwenty = script.EventsAt(20);
    Assert::AreEqual(std::size_t{3}, atTwenty.size());
    Assert::IsTrue(atTwenty[0].kind == Outpost::InputEvent::Kind::Wheel);
    Assert::AreEqual(-2.0f, atTwenty[0].wheelSteps);
    Assert::IsTrue(atTwenty[1].kind == Outpost::InputEvent::Kind::ToggleStatus);
    Assert::IsTrue(atTwenty[2].kind == Outpost::InputEvent::Kind::ResetView);
    Assert::IsTrue(script.EventsAt(6).empty());
  }

  TEST_METHOD(AKeyIsHeldForItsTicksAndAPressForNone)
  {
    const Outpost::InputScript script = Parsed("key tick=5 name=W ticks=30\npress tick=7 name=F9\nmove tick=8 at=1400,600\n");
    const auto& events = script.Events();
    Assert::AreEqual(std::size_t{5}, events.size());
    Assert::IsTrue(events[0].kind == Outpost::InputEvent::Kind::KeyDown);
    Assert::AreEqual(std::uint64_t{5}, events[0].tick);
    Assert::AreEqual(Neuron::InputKey{'W'}, events[0].key);
    Assert::IsTrue(events[1].kind == Outpost::InputEvent::Kind::KeyDown, L"F9 goes down at tick 7");
    Assert::AreEqual(Neuron::InputKey{0x78}, events[1].key);
    Assert::IsTrue(events[2].kind == Outpost::InputEvent::Kind::KeyUp, L"and up in the same tick");
    Assert::AreEqual(std::uint64_t{7}, events[2].tick);
    Assert::IsTrue(events[3].kind == Outpost::InputEvent::Kind::PointerMove);
    Assert::AreEqual(1400.0f, events[3].xPixels);
    Assert::AreEqual(600.0f, events[3].yPixels);
    Assert::IsTrue(events[4].kind == Outpost::InputEvent::Kind::KeyUp);
    Assert::AreEqual(std::uint64_t{35}, events[4].tick, L"W is released 30 ticks after it went down");
    Assert::AreEqual(std::uint64_t{35}, script.LastTick());
  }

  TEST_METHOD(AKeyMustBeOneTheBindingsCanName)
  {
    const std::string error = ParseError("press tick=1 name=Warp\n");
    Assert::IsTrue(error.find("line 1") != std::string::npos, std::wstring(error.begin(), error.end()).c_str());
    Assert::IsFalse(ParseError("key tick=1 name=W ticks=0\n").empty(), L"a key is held for one tick at least");
  }

  TEST_METHOD(TheStoredScriptsParse)
  {
    // A stored script is only run by hand, on WARP, for minutes: a key the bindings lost or a record
    // that no longer parses should fail here first.
    Assert::IsTrue(StoredScript("ScriptedSmoke.txt").LastTick() > 0);
    Assert::IsTrue(StoredScript("FlightSmoke.txt").LastTick() > 0);
  }

  TEST_METHOD(TheFlightScriptStartsByClickingLaunch)
  {
    // The flight run starts docked and leaves by the Services inspector's Launch button. Its click is
    // a pixel of the 1920 x 1080 smoke frame, so it must stay on the button if the docked view moves.
    const Outpost::InputScript script = StoredScript("FlightSmoke.txt");
    Assert::IsTrue(script.Events().size() >= 2);
    const Outpost::InputEvent& press = script.Events()[0];
    const Outpost::InputEvent& release = script.Events()[1];
    Assert::IsTrue(press.kind == Outpost::InputEvent::Kind::PointerDown && press.button == Outpost::PointerButton::Left);
    Assert::IsTrue(release.kind == Outpost::InputEvent::Kind::PointerUp && release.tick == press.tick, L"a click, in one tick");
    Assert::IsTrue(press.tick >= 2, L"after the first frame has laid the controls out");

    Neuron::FontAtlas atlas;
    Assert::IsTrue(atlas.Build(Outpost::TacticalHud::Fonts(1.0f)));
    Neuron::HudBatch batch;
    batch.Begin(&atlas);
    Outpost::DockedViewInput docked; // the smoke frame, at scale 1, with Services open as a new game shows it
    const Outpost::DockedViewLayout layout = Outpost::DockedView{}.Build(batch, docked);
    const auto launch = std::ranges::find(layout.controls, Outpost::DockedView::LAUNCH, &Outpost::DockedControl::id);
    Assert::IsTrue(launch != layout.controls.end(), L"the Services inspector offers Launch");
    Assert::IsTrue(launch->enabled);
    Assert::IsTrue(press.xPixels > launch->rect.left && press.xPixels < launch->rect.right && press.yPixels > launch->rect.top &&
                     press.yPixels < launch->rect.bottom,
                   L"the script's click lands on the Launch button");
  }

  TEST_METHOD(AnEmptyScriptHasNoTicks)
  {
    const Outpost::InputScript script = Parsed("# nothing but a comment\n");
    Assert::IsTrue(script.Events().empty());
    Assert::AreEqual(std::uint64_t{0}, script.LastTick());
  }

  TEST_METHOD(MistakesNameTheirLine)
  {
    Assert::IsTrue(ParseError("help tick=1\njump tick=2\n").find("line 2") != std::string::npos, L"an unknown command");
    Assert::IsTrue(ParseError("click tick=0 at=1,1\n").find("tick") != std::string::npos, L"ticks start at 1");
    Assert::IsTrue(ParseError("click tick=1 at=1\n").find("at") != std::string::npos, L"a point needs x and y");
    Assert::IsTrue(ParseError("click tick=1 at=1,1 button=thumb\n").find("button") != std::string::npos);
    Assert::IsTrue(ParseError("drag tick=1 from=1,1 to=2,2\n").find("ticks") != std::string::npos, L"a drag says how long");
    Assert::IsTrue(ParseError("wheel tick=1\n").find("steps") != std::string::npos);
  }
};

} // namespace GameAppTests
