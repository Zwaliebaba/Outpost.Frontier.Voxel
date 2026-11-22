#include "FileSys.h"
#include "Messages.h"
#include "TickClock.h"
#include "World.h"

#include "CppUnitTest.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

int g_moduleAnchor = 0; // an address inside this DLL, to find the assets above it

constexpr std::uint32_t TICKS_PER_SECOND = Neuron::TickClock::TICKS_PER_SECOND;

[[nodiscard]] std::wstring Wide(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// The shipped content: the shipyard, its berth, and the Lancer the player flies.
void LoadShipped(Outpost::World& _world)
{
  const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
  Assert::IsTrue(root.has_value(), L"Assets/Scene.txt above the test DLL");
  std::string error;
  Assert::IsTrue(_world.LoadContent(*root, error), Wide(error).c_str());
}

[[nodiscard]] double Distance(const Outpost::WorldPosition& _a, const Outpost::WorldPosition& _b)
{
  return std::sqrt((_a.x - _b.x) * (_a.x - _b.x) + (_a.y - _b.y) * (_a.y - _b.y) + (_a.z - _b.z) * (_a.z - _b.z));
}

/// The player's ship at rest at a point in space, as a save would put it there.
void PlaceInSpace(Outpost::World& _world, const Outpost::WorldPosition& _position)
{
  Outpost::SaveGame save = _world.MakeSave();
  save.character.location = Outpost::CharacterLocation{};
  save.character.location.kind = Outpost::LocationKind::Space;
  Outpost::FlightState flight = *_world.Flight(_world.PlayerShip());
  flight.position = _position;
  flight.velocityMetresPerSecond = DirectX::XMFLOAT3{};
  flight.angularVelocityRadiansPerSecond = DirectX::XMFLOAT3{};
  save.character.location.flight = flight;
  std::string error;
  Assert::IsTrue(_world.Restore(save, error), Wide(error).c_str());
}

[[nodiscard]] Outpost::DockPhase PhaseOf(const Outpost::World& _world)
{
  return _world.DockStatus(_world.PlayerShip()).phase;
}

/// Ticks until the player's ship reaches the phase, or the time runs out; the ticks it took.
[[nodiscard]] std::optional<std::uint64_t> TickUntil(Outpost::World& _world, Outpost::DockPhase _phase, std::uint64_t _limitSeconds)
{
  for (std::uint64_t tick = 0; tick < _limitSeconds * TICKS_PER_SECOND; ++tick)
  {
    _world.Tick();
    if (PhaseOf(_world) == _phase)
    {
      return tick + 1;
    }
  }
  return std::nullopt;
}

[[nodiscard]] const Outpost::SpaceSupport::Point& StartBerth(const Outpost::World& _world)
{
  const Outpost::SpaceSupport& support = _world.Support();
  const std::size_t station = support.FindStation(_world.System().startStation);
  Assert::AreNotEqual(Outpost::SpaceSupport::NO_POINT, station);
  return support.Points(station).front();
}

} // namespace

TEST_CLASS(DockingTests)
{
public:
  TEST_METHOD(ANewGameStartsDockedAndProtectedAtTheStartStation)
  {
    Outpost::World world;
    LoadShipped(world);
    std::string error;
    Assert::IsTrue(world.NewGame(error), Wide(error).c_str());
    const Outpost::DockStatusState status = world.DockStatus(world.PlayerShip());
    Assert::IsTrue(status.phase == Outpost::DockPhase::Docked);
    Assert::AreEqual(world.System().startStation, status.station);
    Assert::IsTrue(world.Protected(world.PlayerShip()));
    Assert::IsTrue(Distance(world.Flight(world.PlayerShip())->position, StartBerth(world).path.pad) < 1e-6, L"on the pad");
    Assert::IsFalse(world.TakeSaveRequest(), L"a new game is not itself a save");
  }

  TEST_METHOD(DockingCompletesFromEveryStartPose)
  {
    // In front of the bay, far out on its axis, off to one side above, and beyond the far approach.
    const std::vector<Outpost::WorldPosition> starts{
      {-170.0, -70.0, 25.0}, {0.0, 16.0, -1500.0}, {600.0, 200.0, -800.0}, {-500.0, -300.0, -3000.0}};
    for (const Outpost::WorldPosition& start : starts)
    {
      Outpost::World world;
      LoadShipped(world);
      PlaceInSpace(world, start);
      const std::wstring where = L" from " + std::to_wstring(start.x) + L"," + std::to_wstring(start.y) + L"," + std::to_wstring(start.z);
      const Outpost::ObjectId ship = world.PlayerShip();
      Assert::IsTrue(world.Command(Outpost::DockCommandMessage{world.State().tick, ship, Outpost::DockCommand::Dock}));
      Assert::IsTrue(PhaseOf(world) != Outpost::DockPhase::None, (L"the Dock key is answered" + where).c_str());

      const std::optional<std::uint64_t> ticks = TickUntil(world, Outpost::DockPhase::Docked, 600);
      Assert::IsTrue(ticks.has_value(), (L"docked within ten minutes" + where).c_str());
      Assert::IsTrue(Distance(world.Flight(ship)->position, StartBerth(world).path.pad) <= StartBerth(world).path.radiusMetres,
                     (L"within the point's radius of the pad" + where).c_str());
      for (const Outpost::ContactRecord& contact : world.Contacts())
      {
        const Outpost::WorldObject* body = world.State().FindObject(contact.body);
        Logger::WriteMessage(("contact at tick " + std::to_string(contact.tick) + " with " + (body != nullptr ? body->name : "?") + " at " +
                              std::to_string(contact.relativeSpeedMetresPerSecond) + " m/s\n")
                               .c_str());
      }
      Assert::IsTrue(world.Contacts().empty(), (L"touched nothing on the way in" + where).c_str());
      Assert::IsTrue(world.Protected(ship));
      Assert::IsTrue(world.TakeSaveRequest(), (L"docking asks for a save" + where).c_str());
      Logger::WriteMessage(("docked in " + std::to_string(*ticks / TICKS_PER_SECOND) + " s\n").c_str());
    }
  }

  TEST_METHOD(TheServerSetsThePlayersCommandsAsideWhileItFlies)
  {
    Outpost::World world;
    LoadShipped(world);
    PlaceInSpace(world, Outpost::WorldPosition{0.0, 16.0, -600.0});
    const Outpost::ObjectId ship = world.PlayerShip();
    Assert::IsTrue(world.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Dock}));
    const std::uint64_t diverged = world.DivergedAt(ship);
    for (int i = 0; i < 5 * static_cast<int>(TICKS_PER_SECOND); ++i)
    {
      Outpost::ShipCommand command;
      command.wishes.engineOn = true;
      command.wishes.throttle = 1.0f;
      command.wishes.reverse = true;
      static_cast<void>(world.SubmitCommand(Outpost::FlightInputMessage{world.State().tick + 1, ship, command}));
      world.Tick();
    }
    Assert::AreEqual(diverged, world.DivergedAt(ship), L"no tick the server flew counts as missed");
    Assert::IsTrue(PhaseOf(world) != Outpost::DockPhase::None, L"still docking");
  }

  TEST_METHOD(ALaunchPlacesTheShipClearOfTheStationAndHandsItBack)
  {
    Outpost::World world;
    LoadShipped(world);
    std::string error;
    Assert::IsTrue(world.NewGame(error), Wide(error).c_str());
    const Outpost::ObjectId ship = world.PlayerShip();
    Assert::IsTrue(world.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Launch}));
    Assert::IsTrue(PhaseOf(world) == Outpost::DockPhase::Launching);
    Assert::IsTrue(world.TakeSaveRequest(), L"launching asks for a save");
    Assert::IsTrue(world.MakeSave().character.location.kind == Outpost::LocationKind::Docked, L"saved in the berth it leaves");

    std::uint64_t ticks = 0;
    while (PhaseOf(world) == Outpost::DockPhase::Launching && ticks < 60 * TICKS_PER_SECOND)
    {
      Assert::IsTrue(world.Protected(ship), L"protected while carried out");
      world.Tick();
      ++ticks;
    }
    Assert::IsTrue(PhaseOf(world) == Outpost::DockPhase::None, L"released");
    const Outpost::FlightState& flight = *world.Flight(ship);
    Assert::IsTrue(Distance(flight.position, StartBerth(world).path.entry) < 1e-6, L"released at the entry");
    Assert::AreEqual(static_cast<double>(Outpost::World::LAUNCH_RELEASE_METRES_PER_SECOND),
                     static_cast<double>(flight.SpeedMetresPerSecond()), 1e-3, L"leaving at the release speed");
    Assert::IsFalse(world.Protected(ship));
    Assert::AreEqual(world.State().tick, world.DivergedAt(ship), L"the client is told the whole state again");
    Assert::IsNull(world.Support().HeldBy(ship), L"the berth is free");

    for (int i = 0; i < 5 * static_cast<int>(TICKS_PER_SECOND); ++i)
    {
      world.Tick(); // coasting on, without a command: nothing is in the way
    }
    Assert::IsTrue(world.Contacts().empty(), L"clear of the station");
    Assert::IsTrue(world.MakeSave().character.location.kind == Outpost::LocationKind::Space);
  }

  TEST_METHOD(OnlyCommandsThatFitThePhaseAreTaken)
  {
    Outpost::World world;
    LoadShipped(world);
    std::string error;
    Assert::IsTrue(world.NewGame(error), Wide(error).c_str());
    const Outpost::ObjectId ship = world.PlayerShip();
    Assert::IsFalse(world.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Dock}), L"docked already");
    Assert::IsFalse(world.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Cancel}), L"nothing to cancel");
    Assert::IsFalse(world.Command(Outpost::DockCommandMessage{0, ship + 1, Outpost::DockCommand::Launch}), L"not the player's ship");
    Assert::IsTrue(PhaseOf(world) == Outpost::DockPhase::Docked);
  }

  TEST_METHOD(TheDockKeyIsAnsweredOutOfRangeFarFromAnyStation)
  {
    Outpost::World world;
    LoadShipped(world);
    PlaceInSpace(world, Outpost::WorldPosition{0.0, 0.0, -20'000.0});
    Assert::IsTrue(world.Command(Outpost::DockCommandMessage{0, world.PlayerShip(), Outpost::DockCommand::Dock}));
    const Outpost::DockStatusState status = world.DockStatus(world.PlayerShip());
    Assert::IsTrue(status.phase == Outpost::DockPhase::None);
    Assert::IsTrue(status.answer == Outpost::DockAnswer::OutOfRange);
  }

  TEST_METHOD(CancelingTheApproachFreesThePoint)
  {
    Outpost::World world;
    LoadShipped(world);
    PlaceInSpace(world, Outpost::WorldPosition{0.0, 16.0, -600.0});
    const Outpost::ObjectId ship = world.PlayerShip();
    Assert::IsTrue(world.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Dock}));
    Assert::IsTrue(PhaseOf(world) == Outpost::DockPhase::Staging);
    Assert::IsNotNull(world.Support().HeldBy(ship));
    Assert::IsTrue(world.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Cancel}));
    Assert::IsTrue(world.DockStatus(ship).answer == Outpost::DockAnswer::Canceled);
    Assert::IsTrue(PhaseOf(world) == Outpost::DockPhase::None);
    Assert::IsNull(world.Support().HeldBy(ship));
  }
};

} // namespace GameLogicTests
