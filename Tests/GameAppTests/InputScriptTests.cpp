#include "InputScript.h"

#include <string>

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
