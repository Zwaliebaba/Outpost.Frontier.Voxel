#include "SceneDefinition.h"
#include "TickClock.h"
#include "Universe.h"

#include "CppUnitTest.h"

#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t TICKS_PER_MINUTE = Neuron::TickClock::TICKS_PER_SECOND * 60;

constexpr std::string_view BASE_SCENE = R"(camera target=0,0,0 yaw=0 pitch=20 distance=300 fov=45
model name=Box file=Models/TrussBeam.vox
instance name=A model=Box position=0,0,0
instance name=B model=Box position=50,0,0
ship instance=A callsign=X-1 class=Frigate hull=50 module="Hull Section (B):34" module=Engines:90
ship instance=B callsign=X-2 class=Escort hull=100
objective text="Hold the line" done=true
)";

[[nodiscard]] Outpost::Universe LoadedUniverse(std::string_view _resources)
{
  Outpost::SceneDefinition scene;
  std::string error;
  const std::string text = std::string(BASE_SCENE) + std::string(_resources);
  Assert::IsTrue(Outpost::ParseScene(text, scene, error), std::wstring(error.begin(), error.end()).c_str());
  Outpost::Universe universe;
  universe.Load(scene);
  return universe;
}

void Run(Outpost::Universe& _universe, std::uint32_t _ticks)
{
  for (std::uint32_t i = 0; i < _ticks; ++i)
  {
    _universe.Tick();
  }
}

} // namespace

TEST_CLASS(UniverseTests)
{
public:
  TEST_METHOD(TheFirstSnapshotIsTheScene)
  {
    const Outpost::Universe universe = LoadedUniverse("resource name=Ore amount=100 perminute=120\n");
    const Outpost::UniverseSnapshot& snapshot = universe.State();
    Assert::AreEqual(std::uint64_t{0}, snapshot.tick);
    Assert::AreEqual(std::size_t{2}, snapshot.ships.size());
    Assert::AreEqual(std::size_t{1}, snapshot.objectives.size());
    Assert::IsTrue(snapshot.objectives[0].complete);
    Assert::AreEqual(std::int64_t{100}, snapshot.resources[0].amount);
    const Outpost::ShipState* ship = snapshot.FindShip("A");
    Assert::IsNotNull(ship);
    const std::vector<Outpost::ShipModuleState> damaged = ship->DamagedModules();
    Assert::AreEqual(std::size_t{2}, damaged.size());
    Assert::AreEqual(34, damaged[0].healthPercent, L"worst module first");
    Assert::IsNull(snapshot.FindShip("Nobody"));
  }

  TEST_METHOD(IncomeIsExactPerMinute)
  {
    Outpost::Universe universe = LoadedUniverse("resource name=Ore amount=100 perminute=120\n");
    Run(universe, TICKS_PER_MINUTE);
    Assert::AreEqual(std::int64_t{220}, universe.State().resources[0].amount, L"100 + 120 after exactly one minute");
    Assert::AreEqual(std::uint64_t{TICKS_PER_MINUTE}, universe.TickCount());
  }

  TEST_METHOD(FractionalIncomeIsCarriedNotLost)
  {
    Outpost::Universe universe = LoadedUniverse("resource name=Ore amount=0 perminute=7\n");
    Run(universe, TICKS_PER_MINUTE / 2);
    Assert::AreEqual(std::int64_t{3}, universe.State().resources[0].amount, L"3.5 earned, 3 whole");
    Run(universe, TICKS_PER_MINUTE / 2);
    Assert::AreEqual(std::int64_t{7}, universe.State().resources[0].amount, L"the half carried over is paid");
  }

  TEST_METHOD(CapacityCapsIncome)
  {
    Outpost::Universe universe = LoadedUniverse("resource name=Crew amount=90 capacity=100 perminute=1200\n");
    Run(universe, 40);
    Assert::AreEqual(std::int64_t{100}, universe.State().resources[0].amount);
  }

  TEST_METHOD(LoadingAgainStartsOver)
  {
    Outpost::Universe universe = LoadedUniverse("resource name=Ore amount=100 perminute=120\n");
    Run(universe, 50);
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsTrue(Outpost::ParseScene(std::string(BASE_SCENE), scene, error));
    universe.Load(scene);
    Assert::AreEqual(std::uint64_t{0}, universe.TickCount());
    Assert::IsTrue(universe.State().resources.empty());
  }
};

} // namespace GameLogicTests
