#include "ControlFocus.h"

#include <string_view>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

constexpr Neuron::ControlId LAUNCH = 1;
constexpr Neuron::ControlId REPAIR = 2;
constexpr Neuron::ControlId BUILD = 3;

/// Three buttons in a row; Repair overlaps the right half of Launch and is disabled with a reason.
void Layout(Neuron::ControlFocus& _focus, bool _repairEnabled = false)
{
  _focus.BeginLayout();
  _focus.Add(LAUNCH, Neuron::ScreenRect{0.0f, 0.0f, 100.0f, 40.0f});
  _focus.Add(REPAIR, Neuron::ScreenRect{60.0f, 0.0f, 160.0f, 40.0f}, _repairEnabled, "Hangar destroyed");
  _focus.Add(BUILD, Neuron::ScreenRect{200.0f, 0.0f, 300.0f, 40.0f});
}

} // namespace

TEST_CLASS(ControlFocusTests)
{
public:
  TEST_METHOD(HitTestingFindsTheTopmostControlUnderThePoint)
  {
    Neuron::ControlFocus focus;
    Layout(focus);
    Assert::AreEqual(LAUNCH, focus.HitTest(30.0f, 20.0f));
    Assert::AreEqual(REPAIR, focus.HitTest(80.0f, 20.0f), L"the later control lies on top");
    Assert::AreEqual(BUILD, focus.HitTest(200.0f, 0.0f), L"the top-left edge is inside");
    Assert::AreEqual(Neuron::NO_CONTROL, focus.HitTest(300.0f, 20.0f), L"the right edge is outside");
    Assert::AreEqual(Neuron::NO_CONTROL, focus.HitTest(180.0f, 20.0f));
    Assert::AreEqual(Neuron::NO_CONTROL, focus.HitTest(30.0f, 40.0f), L"the bottom edge is outside");
  }

  TEST_METHOD(HoverPressAndReleaseMakeAClick)
  {
    Neuron::ControlFocus focus;
    Layout(focus);
    Assert::IsTrue(focus.StateOf(BUILD) == Neuron::ControlState::Idle);
    focus.PointerMove(250.0f, 20.0f);
    Assert::IsTrue(focus.StateOf(BUILD) == Neuron::ControlState::Hover);
    Assert::IsTrue(focus.StateOf(LAUNCH) == Neuron::ControlState::Idle);
    Assert::IsTrue(focus.PointerDown(), L"a press on a control is the UI's, not the world's");
    Assert::IsTrue(focus.StateOf(BUILD) == Neuron::ControlState::Pressed);
    Assert::AreEqual(Neuron::NO_CONTROL, focus.TakeClick(), L"no click before the release");
    Layout(focus); // a new frame's layout keeps the press
    focus.PointerUp();
    Assert::AreEqual(BUILD, focus.TakeClick());
    Assert::AreEqual(Neuron::NO_CONTROL, focus.TakeClick(), L"a click is taken once");
    Assert::IsTrue(focus.StateOf(BUILD) == Neuron::ControlState::Hover);

    focus.PointerMove(180.0f, 20.0f);
    Assert::IsFalse(focus.PointerDown(), L"a press on empty space is left to the world");
    focus.PointerUp();
    Assert::AreEqual(Neuron::NO_CONTROL, focus.TakeClick());
  }

  TEST_METHOD(ReleasingAwayFromThePressedControlIsNoClick)
  {
    Neuron::ControlFocus focus;
    Layout(focus);
    focus.PointerMove(20.0f, 20.0f);
    Assert::IsTrue(focus.PointerDown());
    focus.PointerMove(250.0f, 20.0f);
    Assert::IsTrue(focus.StateOf(LAUNCH) == Neuron::ControlState::Idle, L"held, but not under the pointer");
    Assert::IsTrue(focus.StateOf(BUILD) == Neuron::ControlState::Hover, L"hovered, but not the one pressed");
    focus.PointerUp();
    Assert::AreEqual(Neuron::NO_CONTROL, focus.TakeClick());

    // Leaving and coming back before the release still clicks.
    focus.PointerMove(20.0f, 20.0f);
    Assert::IsTrue(focus.PointerDown());
    focus.PointerMove(500.0f, 20.0f);
    focus.PointerMove(20.0f, 20.0f);
    focus.PointerUp();
    Assert::AreEqual(LAUNCH, focus.TakeClick());

    // The pointer leaving the window drops the press.
    Assert::IsTrue(focus.PointerDown());
    focus.PointerLeave();
    focus.PointerUp();
    Assert::AreEqual(Neuron::NO_CONTROL, focus.TakeClick());
    Assert::AreEqual(Neuron::NO_CONTROL, focus.Hovered());
  }

  TEST_METHOD(ADisabledControlReportsItsReasonAndIgnoresClicks)
  {
    Neuron::ControlFocus focus;
    Layout(focus);
    Assert::IsFalse(focus.IsEnabled(REPAIR));
    Assert::IsTrue(focus.Reason(REPAIR) == std::string_view("Hangar destroyed"));
    Assert::IsTrue(focus.Reason(LAUNCH).empty(), L"an enabled control has no reason");
    Assert::IsTrue(focus.StateOf(REPAIR) == Neuron::ControlState::Disabled);

    focus.PointerMove(120.0f, 20.0f);
    Assert::IsTrue(focus.StateOf(REPAIR) == Neuron::ControlState::Disabled, L"hover does not change a disabled control");
    Assert::IsTrue(focus.PointerDown(), L"the press is still the UI's");
    Assert::IsTrue(focus.StateOf(REPAIR) == Neuron::ControlState::Disabled);
    focus.PointerUp();
    Assert::AreEqual(Neuron::NO_CONTROL, focus.TakeClick(), L"a disabled control is never clicked");
    Assert::AreEqual(REPAIR, focus.TakeRefusal(), L"the press is reported as a refusal, so the reason can be shown");
    Assert::AreEqual(Neuron::NO_CONTROL, focus.TakeRefusal());

    Layout(focus, true);
    Assert::IsTrue(focus.Reason(REPAIR).empty());
    Assert::IsTrue(focus.StateOf(REPAIR) == Neuron::ControlState::Hover, L"enabled again, it reacts to the pointer");
  }

  TEST_METHOD(KeyboardFocusSkipsDisabledControlsAndActivates)
  {
    Neuron::ControlFocus focus;
    Layout(focus);
    Assert::AreEqual(Neuron::NO_CONTROL, focus.Focused());
    focus.FocusNext();
    Assert::AreEqual(LAUNCH, focus.Focused());
    focus.FocusNext();
    Assert::AreEqual(BUILD, focus.Focused(), L"the disabled control is skipped");
    focus.FocusNext();
    Assert::AreEqual(LAUNCH, focus.Focused(), L"focus wraps");
    focus.FocusPrevious();
    Assert::AreEqual(BUILD, focus.Focused());
    Assert::IsTrue(focus.StateOf(BUILD) == Neuron::ControlState::Hover, L"focus shows like hover");
    focus.Activate();
    Assert::AreEqual(BUILD, focus.TakeClick());
    focus.ClearFocus();
    focus.Activate();
    Assert::AreEqual(Neuron::NO_CONTROL, focus.TakeClick());

    Neuron::ControlFocus empty;
    empty.FocusPrevious();
    Assert::AreEqual(Neuron::NO_CONTROL, empty.Focused());
  }

  TEST_METHOD(TheAuthorityStatesWinOverThePointer)
  {
    Neuron::ControlFocus focus;
    Layout(focus);
    focus.PointerMove(250.0f, 20.0f);
    Assert::IsTrue(focus.StateOf(BUILD, Neuron::ControlState::Selected) == Neuron::ControlState::Selected);
    Assert::IsTrue(focus.PointerDown());
    Assert::IsTrue(focus.StateOf(BUILD, Neuron::ControlState::Selected) == Neuron::ControlState::Pressed);
    for (const Neuron::ControlState state :
         {Neuron::ControlState::Pending, Neuron::ControlState::Confirmed, Neuron::ControlState::Refused, Neuron::ControlState::Disabled})
    {
      Assert::IsTrue(focus.StateOf(BUILD, state) == state, L"a reply from the authority is shown whatever the pointer does");
    }
    Assert::IsTrue(focus.StateOf(REPAIR, Neuron::ControlState::Pending) == Neuron::ControlState::Disabled,
                   L"a disabled control stays disabled");
  }
};

} // namespace NeuronClientTests
