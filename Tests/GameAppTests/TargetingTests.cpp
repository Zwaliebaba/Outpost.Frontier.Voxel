#include "Targeting.h"

#include "CppUnitTest.h"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameAppTests
{

namespace
{

// The own ship at the origin, facing +Z.
const Outpost::UniversePosition HERE{0.0, 0.0, 0.0};
constexpr DirectX::XMFLOAT3 AHEAD{0.0f, 0.0f, 1.0f};

[[nodiscard]] std::vector<Outpost::TargetCandidate> Contacts()
{
  return {
    Outpost::TargetCandidate{1, Outpost::Owner::Hostile, {0.0, 0.0, -100.0}, true}, // nearest, behind
    Outpost::TargetCandidate{2, Outpost::Owner::Hostile, {0.0, 0.0, 400.0}, true},  // ahead
    Outpost::TargetCandidate{3, Outpost::Owner::Hostile, {0.0, 0.0, 900.0}, true},  // ahead, farther
    Outpost::TargetCandidate{4, Outpost::Owner::Neutral, {0.0, 0.0, 50.0}, true},   // not hostile
    Outpost::TargetCandidate{5, Outpost::Owner::Neutral, {0.0, 0.0, 60.0}, false},  // a station
  };
}

} // namespace

TEST_CLASS(TargetingTests)
{
public:
  TEST_METHOD(TTakesTheNearestHostileInFrontThenCycles)
  {
    const std::vector<Outpost::TargetCandidate> contacts = Contacts();
    Outpost::Targeting targeting;
    targeting.Next(contacts, HERE, AHEAD);
    Assert::AreEqual(Outpost::ObjectId{2}, targeting.Target(), L"the nearest hostile in front, not the nearest behind");
    targeting.Next(contacts, HERE, AHEAD);
    Assert::AreEqual(Outpost::ObjectId{3}, targeting.Target(), L"then the next nearest");
    targeting.Next(contacts, HERE, AHEAD);
    Assert::AreEqual(Outpost::ObjectId{1}, targeting.Target(), L"round to the nearest of all");
    targeting.Next(contacts, HERE, AHEAD);
    Assert::AreEqual(Outpost::ObjectId{2}, targeting.Target());
  }

  TEST_METHOD(WithNoneInFrontTTakesTheNearest)
  {
    const std::vector<Outpost::TargetCandidate> contacts = Contacts();
    Outpost::Targeting targeting;
    targeting.Next(contacts, HERE, DirectX::XMFLOAT3{1.0f, 0.0f, 0.0f});
    Assert::AreEqual(Outpost::ObjectId{1}, targeting.Target());
    // From a target that is not hostile, T starts again from the front.
    targeting.Set(5);
    targeting.Next(contacts, HERE, AHEAD);
    Assert::AreEqual(Outpost::ObjectId{2}, targeting.Target());
  }

  TEST_METHOD(ModulesCycleThroughTheWholeShipBothWays)
  {
    Outpost::Targeting targeting;
    targeting.CycleModule(1, 3);
    Assert::AreEqual(Outpost::Targeting::WHOLE_SHIP, targeting.Module(), L"no target, no module");
    targeting.Set(2);
    targeting.CycleModule(1, 3);
    Assert::AreEqual(0, targeting.Module());
    targeting.CycleModule(1, 3);
    targeting.CycleModule(1, 3);
    Assert::AreEqual(2, targeting.Module());
    targeting.CycleModule(1, 3);
    Assert::AreEqual(Outpost::Targeting::WHOLE_SHIP, targeting.Module());
    targeting.CycleModule(-1, 3);
    Assert::AreEqual(2, targeting.Module());
    targeting.Set(3);
    Assert::AreEqual(Outpost::Targeting::WHOLE_SHIP, targeting.Module(), L"a new target starts whole");
  }

  TEST_METHOD(ATargetThatIsGoneIsForgotten)
  {
    std::vector<Outpost::TargetCandidate> contacts = Contacts();
    Outpost::Targeting targeting;
    targeting.Set(3);
    targeting.Keep(contacts);
    Assert::AreEqual(Outpost::ObjectId{3}, targeting.Target());
    contacts.erase(contacts.begin() + 2);
    targeting.Keep(contacts);
    Assert::AreEqual(Outpost::NO_OBJECT, targeting.Target());
    targeting.Set(4);
    targeting.Clear();
    Assert::AreEqual(Outpost::NO_OBJECT, targeting.Target());
  }
};

} // namespace GameAppTests
