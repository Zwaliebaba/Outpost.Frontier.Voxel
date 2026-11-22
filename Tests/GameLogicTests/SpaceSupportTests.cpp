#include "DockingPoint.h"
#include "SpaceSupport.h"

#include "CppUnitTest.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Outpost::DockAnswer;
using Outpost::DockKind;
using Outpost::SpaceSupport;

[[nodiscard]] SpaceSupport::Point PointAt(double _x, DockKind _kind = DockKind::Berth)
{
  SpaceSupport::Point point;
  point.instance = "Station";
  point.name = "Point" + std::to_string(static_cast<int>(_x));
  point.kind = _kind;
  point.path.entry = Outpost::WorldPosition{_x, 0.0, -100.0};
  point.path.approach = Outpost::WorldPosition{_x, 0.0, -20.0};
  point.path.pad = Outpost::WorldPosition{_x, 0.0, 0.0};
  return point;
}

const Outpost::WorldPosition HERE{0.0, 0.0, -500.0};

} // namespace

TEST_CLASS(SpaceSupportTests)
{
public:
  TEST_METHOD(TheChecksRefuseInTheChaptersOrder)
  {
    SpaceSupport support;
    const std::size_t bare = support.AddStation("Bare", {});
    const std::size_t medium = support.AddStation("Medium", {PointAt(0.0, DockKind::Medium)});
    const std::size_t moving = support.AddStation("Moving", {PointAt(0.0)}, false);
    const std::size_t berth = support.AddStation("Berth", {PointAt(0.0)});

    // 4 before 5: a station without points says so, whatever the ship's class.
    Assert::IsTrue(support.Request(1, DockKind::Medium, HERE, bare, true, 0).answer == DockAnswer::NoDock);
    // 5: no point accepts the class.
    Assert::IsTrue(support.Request(1, DockKind::Berth, HERE, medium, true, 0).answer == DockAnswer::CannotDock);
    // 7: free, but the station is not at rest.
    Assert::IsTrue(support.Request(2, DockKind::Berth, HERE, moving, true, 0).answer == DockAnswer::Wait);
    // 6 before 7: once held, a second ship waits for the point first.
    const SpaceSupport::Answer first = support.Request(3, DockKind::Berth, HERE, berth, true, 0);
    Assert::IsTrue(first.answer == DockAnswer::Proceed);
    Assert::AreEqual(std::size_t{0}, first.point);
    Assert::IsTrue(support.Request(4, DockKind::Berth, HERE, berth, true, 0).answer == DockAnswer::Wait);
    // Refused ships are on no list.
    Assert::IsNull(support.HeldBy(1));
    Assert::IsFalse(support.Waiting(1));
  }

  TEST_METHOD(ProceedPicksTheNearestFreeFittingPoint)
  {
    SpaceSupport support;
    const std::size_t station =
      support.AddStation("Station", {PointAt(-300.0), PointAt(40.0, DockKind::Medium), PointAt(60.0), PointAt(200.0)});
    Assert::AreEqual(std::size_t{2}, support.Request(1, DockKind::Berth, HERE, station, true, 0).point, L"the nearest berth");
    Assert::AreEqual(std::size_t{3}, support.Request(2, DockKind::Berth, HERE, station, true, 0).point, L"the next, once it is held");
    Assert::AreEqual(std::size_t{2}, support.Request(1, DockKind::Berth, HERE, station, true, 0).point, L"asked again, the same");
  }

  TEST_METHOD(TwoShipsQueueForOnePad)
  {
    SpaceSupport support;
    const std::size_t station = support.AddStation("Station", {PointAt(0.0)});
    Assert::IsTrue(support.Request(1, DockKind::Berth, HERE, station, true, 0).answer == DockAnswer::Proceed);
    Assert::IsTrue(support.Request(2, DockKind::Berth, HERE, station, true, 10).answer == DockAnswer::Wait);

    support.Docked(1);
    SpaceSupport::Heartbeat beat = support.Beat(60);
    Assert::IsTrue(beat.cleared.empty(), L"a docked ship keeps its berth");
    Assert::IsTrue(support.Waiting(2));

    support.Release(1); // launched
    beat = support.Beat(120);
    Assert::AreEqual(std::size_t{1}, beat.cleared.size());
    Assert::AreEqual(Outpost::ObjectId{2}, beat.cleared.front().ship);
    Assert::IsNotNull(support.HeldBy(2));
    Assert::IsFalse(support.Waiting(2));
  }

  TEST_METHOD(PlayersWaitAheadOfOtherShipsAndBehindEarlierPlayers)
  {
    SpaceSupport support;
    const std::size_t station = support.AddStation("Station", {PointAt(0.0)});
    Assert::IsTrue(support.Request(1, DockKind::Berth, HERE, station, true, 0).answer == DockAnswer::Proceed);
    static_cast<void>(support.Request(10, DockKind::Berth, HERE, station, false, 0)); // an NPC
    static_cast<void>(support.Request(2, DockKind::Berth, HERE, station, true, 0));   // a player, ahead of it
    static_cast<void>(support.Request(3, DockKind::Berth, HERE, station, true, 0));   // a later player, behind the first
    Assert::AreEqual(std::size_t{3}, support.WaitingCount());

    std::vector<Outpost::ObjectId> order;
    for (int i = 0; i < 3; ++i)
    {
      support.Release(order.empty() ? 1 : order.back());
      for (const SpaceSupport::Clearance& cleared : support.Beat(60).cleared)
      {
        order.push_back(cleared.ship);
      }
    }
    Assert::IsTrue(order == std::vector<Outpost::ObjectId>{2, 3, 10});
  }

  TEST_METHOD(AShipNotDockedNinetySecondsAfterClearanceIsThrownOut)
  {
    SpaceSupport support;
    const std::size_t station = support.AddStation("Station", {PointAt(0.0)});
    Assert::IsTrue(support.Request(1, DockKind::Berth, HERE, station, true, 100).answer == DockAnswer::Proceed);
    Assert::IsTrue(support.Request(2, DockKind::Berth, HERE, station, false, 100).answer == DockAnswer::Wait);

    Assert::IsTrue(support.Beat(100 + SpaceSupport::ABANDON_TICKS - 1).abandoned.empty(), L"not a tick early");
    const SpaceSupport::Heartbeat beat = support.Beat(100 + SpaceSupport::ABANDON_TICKS);
    Assert::IsTrue(beat.abandoned == std::vector<Outpost::ObjectId>{1});
    Assert::IsNull(support.HeldBy(1));
    Assert::AreEqual(std::size_t{1}, beat.cleared.size(), L"the waiting ship takes the freed point");
    Assert::AreEqual(Outpost::ObjectId{2}, beat.cleared.front().ship);
  }

  TEST_METHOD(ADockedShipIsNeverThrownOut)
  {
    SpaceSupport support;
    const std::size_t station = support.AddStation("Station", {PointAt(0.0)});
    support.Occupy(1, station, 0);
    Assert::IsTrue(support.Beat(SpaceSupport::ABANDON_TICKS * 10).abandoned.empty());
    Assert::IsNotNull(support.HeldBy(1));
    Assert::IsTrue(support.Request(2, DockKind::Berth, HERE, station, true, 0).answer == DockAnswer::Wait);
  }
};

} // namespace GameLogicTests
