#include "FileSys.h"
#include "Messages.h"
#include "SaveGame.h"
#include "SceneDefinition.h"
#include "ShipArchetype.h"
#include "SystemContent.h"
#include "TickClock.h"
#include "Universe.h"

#include "CppUnitTest.h"

#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
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
void LoadShipped(Outpost::Universe& _universe)
{
  const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
  Assert::IsTrue(root.has_value(), L"Assets/Scene.txt above the test DLL");
  std::string error;
  Assert::IsTrue(_universe.LoadContent(*root, error), Wide(error).c_str());
}

/// The shipped content with its ship parts changed first: a ship that cannot fly, for instance.
template <typename Change> void LoadShippedWith(Outpost::Universe& _universe, Change _change)
{
  const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
  Assert::IsTrue(root.has_value(), L"Assets/Scene.txt above the test DLL");
  std::string error;
  Outpost::SceneDefinition scene;
  Outpost::ShipContent ships;
  Outpost::SystemContent system;
  Assert::IsTrue(Outpost::LoadSceneContent(*root, scene, error), Wide(error).c_str());
  Assert::IsTrue(Outpost::LoadShipContent(*root / "Content" / "Ships.txt", ships, error), Wide(error).c_str());
  Assert::IsTrue(Outpost::LoadSystemContent(*root / "Content" / "System.txt", scene, system, error), Wide(error).c_str());
  _change(ships);
  _universe.Load(scene, ships, system);
}

[[nodiscard]] double Distance(const Outpost::UniversePosition& _a, const Outpost::UniversePosition& _b)
{
  return std::sqrt((_a.x - _b.x) * (_a.x - _b.x) + (_a.y - _b.y) * (_a.y - _b.y) + (_a.z - _b.z) * (_a.z - _b.z));
}

/// The player's ship at rest at a point in space, as a save would put it there.
void PlaceInSpace(Outpost::Universe& _universe, const Outpost::UniversePosition& _position)
{
  Outpost::SaveGame save = _universe.MakeSave();
  save.character.location = Outpost::CharacterLocation{};
  save.character.location.kind = Outpost::LocationKind::Space;
  Outpost::FlightState flight = *_universe.Flight(_universe.PlayerShip());
  flight.position = _position;
  flight.velocityMetresPerSecond = DirectX::XMFLOAT3{};
  flight.angularVelocityRadiansPerSecond = DirectX::XMFLOAT3{};
  save.character.location.flight = flight;
  std::string error;
  Assert::IsTrue(_universe.Restore(save, error), Wide(error).c_str());
}

[[nodiscard]] Outpost::DockPhase PhaseOf(const Outpost::Universe& _universe)
{
  return _universe.DockStatus(_universe.PlayerShip()).phase;
}

/// Ticks until the player's ship reaches the phase, or the time runs out; the ticks it took.
[[nodiscard]] std::optional<std::uint64_t> TickUntil(Outpost::Universe& _universe, Outpost::DockPhase _phase, std::uint64_t _limitSeconds)
{
  for (std::uint64_t tick = 0; tick < _limitSeconds * TICKS_PER_SECOND; ++tick)
  {
    _universe.Tick();
    if (PhaseOf(_universe) == _phase)
    {
      return tick + 1;
    }
  }
  return std::nullopt;
}

[[nodiscard]] const Outpost::SpaceSupport::Point& StartBerth(const Outpost::Universe& _universe)
{
  const Outpost::SpaceSupport& support = _universe.Support();
  const std::size_t station = support.FindStation(_universe.System().startStation);
  Assert::AreNotEqual(Outpost::SpaceSupport::NO_POINT, station);
  return support.Points(station).front();
}

} // namespace

TEST_CLASS(DockingTests)
{
public:
  TEST_METHOD(ANewGameStartsDockedAndProtectedAtTheStartStation)
  {
    Outpost::Universe universe;
    LoadShipped(universe);
    std::string error;
    Assert::IsTrue(universe.NewGame(error), Wide(error).c_str());
    const Outpost::DockStatusState status = universe.DockStatus(universe.PlayerShip());
    Assert::IsTrue(status.phase == Outpost::DockPhase::Docked);
    Assert::AreEqual(universe.System().startStation, status.station);
    Assert::IsTrue(universe.Protected(universe.PlayerShip()));
    Assert::IsTrue(Distance(universe.Flight(universe.PlayerShip())->position, StartBerth(universe).path.pad) < 1e-6, L"on the pad");
    Assert::IsFalse(universe.TakeSaveRequest(), L"a new game is not itself a save");
  }

  TEST_METHOD(DockingCompletesFromEveryStartPose)
  {
    // In front of the bay, far out on its axis, off to one side above, and beyond the far approach.
    const std::vector<Outpost::UniversePosition> starts{
      {-170.0, -70.0, 25.0}, {0.0, 16.0, -1500.0}, {600.0, 200.0, -800.0}, {-500.0, -300.0, -3000.0}};
    for (const Outpost::UniversePosition& start : starts)
    {
      Outpost::Universe universe;
      LoadShipped(universe);
      PlaceInSpace(universe, start);
      const std::wstring where = L" from " + std::to_wstring(start.x) + L"," + std::to_wstring(start.y) + L"," + std::to_wstring(start.z);
      const Outpost::ObjectId ship = universe.PlayerShip();
      Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{universe.State().tick, ship, Outpost::DockCommand::Dock}));
      Assert::IsTrue(PhaseOf(universe) != Outpost::DockPhase::None, (L"the Dock key is answered" + where).c_str());

      const std::optional<std::uint64_t> ticks = TickUntil(universe, Outpost::DockPhase::Docked, 600);
      Assert::IsTrue(ticks.has_value(), (L"docked within ten minutes" + where).c_str());
      Assert::IsTrue(Distance(universe.Flight(ship)->position, StartBerth(universe).path.pad) <= StartBerth(universe).path.radiusMetres,
                     (L"within the point's radius of the pad" + where).c_str());
      for (const Outpost::ContactRecord& contact : universe.Contacts())
      {
        const Outpost::UniverseObject* body = universe.State().FindObject(contact.body);
        Logger::WriteMessage(("contact at tick " + std::to_string(contact.tick) + " with " + (body != nullptr ? body->name : "?") + " at " +
                              std::to_string(contact.relativeSpeedMetresPerSecond) + " m/s\n")
                               .c_str());
      }
      Assert::IsTrue(universe.Contacts().empty(), (L"touched nothing on the way in" + where).c_str());
      Assert::IsTrue(universe.Protected(ship));
      Assert::IsTrue(universe.TakeSaveRequest(), (L"docking asks for a save" + where).c_str());
      Logger::WriteMessage(("docked in " + std::to_string(*ticks / TICKS_PER_SECOND) + " s\n").c_str());
    }
  }

  TEST_METHOD(TheServerSetsThePlayersCommandsAsideWhileItFlies)
  {
    Outpost::Universe universe;
    LoadShipped(universe);
    PlaceInSpace(universe, Outpost::UniversePosition{0.0, 16.0, -600.0});
    const Outpost::ObjectId ship = universe.PlayerShip();
    Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Dock}));
    const std::uint64_t taken = universe.State().tick + 1;
    for (int i = 0; i < 5 * static_cast<int>(TICKS_PER_SECOND); ++i)
    {
      Outpost::ShipCommand command;
      command.wishes.engineOn = true;
      command.wishes.throttle = 1.0f;
      command.wishes.reverse = true;
      static_cast<void>(universe.SubmitCommand(Outpost::FlightInputMessage{universe.State().tick + 1, ship, command}));
      universe.Tick();
    }
    // The first tick the server flies, the client predicted on its own command, not having heard of
    // the dock yet: that one tick diverges, and no later one counts as missed.
    Assert::AreEqual(taken, universe.DivergedAt(ship), L"only the takeover tick diverges");
    Assert::IsTrue(PhaseOf(universe) != Outpost::DockPhase::None, L"still docking");
  }

  TEST_METHOD(ALaunchPlacesTheShipClearOfTheStationAndHandsItBack)
  {
    Outpost::Universe universe;
    LoadShipped(universe);
    std::string error;
    Assert::IsTrue(universe.NewGame(error), Wide(error).c_str());
    const Outpost::ObjectId ship = universe.PlayerShip();
    Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Launch}));
    Assert::IsTrue(PhaseOf(universe) == Outpost::DockPhase::Launching);
    Assert::IsTrue(universe.TakeSaveRequest(), L"launching asks for a save");
    Assert::IsTrue(universe.MakeSave().character.location.kind == Outpost::LocationKind::Docked, L"saved in the berth it leaves");

    std::uint64_t ticks = 0;
    while (PhaseOf(universe) == Outpost::DockPhase::Launching && ticks < std::uint64_t{60} * TICKS_PER_SECOND)
    {
      Assert::IsTrue(universe.Protected(ship), L"protected while carried out");
      universe.Tick();
      ++ticks;
    }
    Assert::IsTrue(PhaseOf(universe) == Outpost::DockPhase::None, L"released");
    const Outpost::FlightState& flight = *universe.Flight(ship);
    Assert::IsTrue(Distance(flight.position, StartBerth(universe).path.entry) < 1e-6, L"released at the entry");
    Assert::AreEqual(static_cast<double>(Outpost::Universe::LAUNCH_RELEASE_METRES_PER_SECOND),
                     static_cast<double>(flight.SpeedMetresPerSecond()), 1e-3, L"leaving at the release speed");
    Assert::IsFalse(universe.Protected(ship));
    Assert::AreEqual(universe.State().tick, universe.DivergedAt(ship), L"the client is told the whole state again");
    Assert::IsNull(universe.Support().HeldBy(ship), L"the berth is free");

    for (int i = 0; i < 5 * static_cast<int>(TICKS_PER_SECOND); ++i)
    {
      universe.Tick(); // coasting on, without a command: nothing is in the way
    }
    Assert::IsTrue(universe.Contacts().empty(), L"clear of the station");
    Assert::IsTrue(universe.MakeSave().character.location.kind == Outpost::LocationKind::Space);
  }

  TEST_METHOD(OnlyCommandsThatFitThePhaseAreTaken)
  {
    Outpost::Universe universe;
    LoadShipped(universe);
    std::string error;
    Assert::IsTrue(universe.NewGame(error), Wide(error).c_str());
    const Outpost::ObjectId ship = universe.PlayerShip();
    Assert::IsFalse(universe.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Dock}), L"docked already");
    Assert::IsFalse(universe.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Cancel}), L"nothing to cancel");
    Assert::IsFalse(universe.Command(Outpost::DockCommandMessage{0, ship + 1, Outpost::DockCommand::Launch}), L"not the player's ship");
    Assert::IsTrue(PhaseOf(universe) == Outpost::DockPhase::Docked);
  }

  TEST_METHOD(TheDockKeyIsAnsweredOutOfRangeFarFromAnyStation)
  {
    Outpost::Universe universe;
    LoadShipped(universe);
    PlaceInSpace(universe, Outpost::UniversePosition{0.0, 0.0, -20'000.0});
    Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{0, universe.PlayerShip(), Outpost::DockCommand::Dock}));
    const Outpost::DockStatusState status = universe.DockStatus(universe.PlayerShip());
    Assert::IsTrue(status.phase == Outpost::DockPhase::None);
    Assert::IsTrue(status.answer == Outpost::DockAnswer::OutOfRange);
  }

  TEST_METHOD(CancelingTheApproachFreesThePoint)
  {
    Outpost::Universe universe;
    LoadShipped(universe);
    PlaceInSpace(universe, Outpost::UniversePosition{0.0, 16.0, -600.0});
    const Outpost::ObjectId ship = universe.PlayerShip();
    Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Dock}));
    Assert::IsTrue(PhaseOf(universe) == Outpost::DockPhase::Staging);
    Assert::IsNotNull(universe.Support().HeldBy(ship));
    Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Cancel}));
    Assert::IsTrue(universe.DockStatus(ship).answer == Outpost::DockAnswer::Canceled);
    Assert::IsTrue(PhaseOf(universe) == Outpost::DockPhase::None);
    Assert::IsNull(universe.Support().HeldBy(ship));
  }

  TEST_METHOD(LaunchFlyDockQuitAndRestartKeepsEveryField)
  {
    Outpost::Universe universe;
    LoadShipped(universe);
    std::string error;
    Assert::IsTrue(universe.NewGame(error), Wide(error).c_str());
    const Outpost::ObjectId ship = universe.PlayerShip();
    Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Launch}));
    Assert::IsTrue(TickUntil(universe, Outpost::DockPhase::None, 60).has_value(), L"launched");
    for (int i = 0; i < 4 * static_cast<int>(TICKS_PER_SECOND); ++i)
    {
      Outpost::ShipCommand command;
      command.wishes.engineOn = true;
      command.wishes.throttle = 0.5f;
      Assert::IsTrue(universe.SubmitCommand(Outpost::FlightInputMessage{universe.State().tick + 1, ship, command}));
      universe.Tick();
    }
    Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{universe.State().tick, ship, Outpost::DockCommand::Dock}));
    Assert::IsTrue(TickUntil(universe, Outpost::DockPhase::Docked, 600).has_value(), L"docked again");
    Assert::IsTrue(universe.TakeSaveRequest());
    for (int i = 0; i < 90; ++i)
    {
      universe.Tick(); // income runs on while docked; the quit save takes it
    }

    // Quit: the save as the executable writes it, as text.
    std::string text;
    Assert::IsTrue(Outpost::FormatSaveGame(universe.MakeSave(), text, error), Wide(error).c_str());
    // Restart: a fresh process loads its content and resumes the save.
    Outpost::Universe resumed;
    LoadShipped(resumed);
    Outpost::SaveGame save;
    Assert::IsTrue(Outpost::ParseSaveGame(text, save, error), Wide(error).c_str());
    Assert::IsTrue(resumed.Restore(save, error), Wide(error).c_str());

    Assert::IsTrue(resumed.MakeSave() == universe.MakeSave(), L"the character and the universe, field by field");
    Assert::AreEqual(universe.State().tick, resumed.State().tick);
    Assert::IsTrue(*resumed.Flight(ship) == *universe.Flight(ship), L"in the same berth, at rest");
    Assert::IsTrue(resumed.DockStatus(ship).phase == Outpost::DockPhase::Docked);
    Assert::AreEqual(universe.DockStatus(ship).station, resumed.DockStatus(ship).station);
    Assert::IsTrue(resumed.State().resources == universe.State().resources);
    for (int i = 0; i < 600; ++i)
    {
      universe.Tick();
      resumed.Tick();
    }
    Assert::IsTrue(resumed.State().resources == universe.State().resources, L"the ledger runs on alike, remainders included");
  }

  TEST_METHOD(QuittingInSpaceKeepsTheSortie)
  {
    Outpost::Universe universe;
    LoadShipped(universe);
    std::string error;
    Assert::IsTrue(universe.NewGame(error), Wide(error).c_str());
    const Outpost::ObjectId ship = universe.PlayerShip();
    Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Launch}));
    Assert::IsTrue(TickUntil(universe, Outpost::DockPhase::None, 60).has_value());
    for (int i = 0; i < 2 * static_cast<int>(TICKS_PER_SECOND); ++i)
    {
      universe.Tick(); // coasting out, turning nothing
    }
    std::string text;
    Assert::IsTrue(Outpost::FormatSaveGame(universe.MakeSave(), text, error), Wide(error).c_str());
    Outpost::Universe resumed;
    LoadShipped(resumed);
    Outpost::SaveGame save;
    Assert::IsTrue(Outpost::ParseSaveGame(text, save, error), Wide(error).c_str());
    Assert::IsTrue(save.character.location.kind == Outpost::LocationKind::Space);
    Assert::IsTrue(resumed.Restore(save, error), Wide(error).c_str());
    Assert::IsTrue(*resumed.Flight(ship) == *universe.Flight(ship), L"the flight state to the bit");
    Assert::IsTrue(resumed.DockStatus(ship).phase == Outpost::DockPhase::None);
    Assert::IsNull(resumed.Support().HeldBy(ship), L"the berth is free");
    for (int i = 0; i < 120; ++i)
    {
      universe.Tick();
      resumed.Tick();
    }
    Assert::IsTrue(*resumed.Flight(ship) == *universe.Flight(ship), L"and it flies on the same");
  }

  TEST_METHOD(TheStoredHazardSaveResumesInSpaceInsideTheBreach)
  {
    // Design/Measurements/HazardSmokeSave.txt is the save CI's smoke run resumes to draw the hazard
    // warning (plan task 2.8). It has to keep fitting the content, and keep lying in the zone.
    std::filesystem::path directory = Neuron::ModuleDirectory(&g_moduleAnchor);
    std::filesystem::path file;
    for (int parents = 0; parents <= 4 && file.empty(); ++parents)
    {
      const std::filesystem::path candidate = directory / "Design" / "Measurements" / "HazardSmokeSave.txt";
      std::error_code ignored;
      if (std::filesystem::is_regular_file(candidate, ignored))
      {
        file = candidate;
      }
      directory = directory.parent_path();
    }
    Assert::IsFalse(file.empty(), L"the save is found in Design/Measurements above the test DLL");
    std::string text;
    Assert::IsTrue(Neuron::ReadTextFile(file, text));

    Outpost::Universe universe;
    LoadShipped(universe);
    Outpost::SaveGame save;
    std::string error;
    Assert::IsTrue(Outpost::ParseSaveGame(text, save, error), Wide(error).c_str());
    Assert::IsTrue(universe.Restore(save, error), Wide(error).c_str());
    const Outpost::ObjectId ship = universe.PlayerShip();
    Assert::IsTrue(universe.DockStatus(ship).phase == Outpost::DockPhase::None, L"in space, flying free");
    Assert::IsFalse(universe.Protected(ship));

    const Outpost::FlightState* flight = universe.Flight(ship);
    Assert::IsNotNull(flight);
    const Outpost::HazardExposure exposure = Outpost::HazardAt(universe.System().zones, flight->position);
    Assert::IsNotNull(exposure.zone, L"inside a physical zone");
    Assert::AreEqual(std::string("FrigateBreach"), exposure.zone->name);
    Assert::IsTrue(exposure.immersion > 0.2f, L"well inside its edge");
    Assert::IsTrue(exposure.damagePerSecond > 0.0f && exposure.interference > 0.0f);

    // At rest there, clear of the frigate above: a second of ticks moves nothing and touches nothing.
    const Outpost::UniversePosition before = flight->position;
    for (std::uint32_t tick = 0; tick < TICKS_PER_SECOND; ++tick)
    {
      universe.Tick();
    }
    Assert::IsTrue(Distance(before, universe.Flight(ship)->position) < 0.01);
    Assert::IsTrue(universe.Contacts().empty());
  }

  TEST_METHOD(ASaveThatDoesNotFitTheContentIsRefusedAndChangesNothing)
  {
    Outpost::Universe universe;
    LoadShipped(universe);
    std::string error;
    Assert::IsTrue(universe.NewGame(error), Wide(error).c_str());
    const Outpost::SaveGame before = universe.MakeSave();

    Outpost::SaveGame save = before;
    save.character.location.station = "Elsewhere";
    save.universe.tick = 999;
    Assert::IsFalse(universe.Restore(save, error));
    Assert::IsTrue(error.find("Elsewhere") != std::string::npos, Wide(error).c_str());
    save = before;
    save.character.ship.instance = "Talon";
    Assert::IsFalse(universe.Restore(save, error), L"another ship than the scene gives the player");
    save = before;
    save.universe.resources.front().name = "Unobtainium";
    Assert::IsFalse(universe.Restore(save, error));
    Assert::IsTrue(universe.MakeSave() == before, L"a refused save leaves the universe as it was");
  }

  TEST_METHOD(ALaunchIsRefusedWithoutAnEngineOrAPowerPlant)
  {
    const auto check = [](auto _change, const std::string& _reason)
    {
      Outpost::Universe universe;
      LoadShippedWith(universe, _change);
      std::string error;
      Assert::IsTrue(universe.NewGame(error), Wide(error).c_str());
      const Outpost::ObjectId ship = universe.PlayerShip();
      Assert::IsTrue(universe.Command(Outpost::DockCommandMessage{0, ship, Outpost::DockCommand::Launch}), L"answered");
      const Outpost::DockStatusState status = universe.DockStatus(ship);
      Assert::IsTrue(status.phase == Outpost::DockPhase::Docked, L"still docked");
      Assert::IsTrue(status.answer == Outpost::DockAnswer::LaunchRefused);
      Assert::AreEqual(_reason, status.reason);
      Assert::IsFalse(universe.TakeSaveRequest(), L"a refused launch is not a launch");
      universe.Tick();
      Assert::IsTrue(universe.DockStatus(ship).phase == Outpost::DockPhase::Docked);
    };
    check(
      [](Outpost::ShipContent& _ships)
      {
        for (Outpost::EngineArchetype& engine : _ships.engines)
        {
          engine.thrustNewtons = 0.0f;
        }
      },
      "the ship has no working engine");
    check(
      [](Outpost::ShipContent& _ships)
      {
        for (Outpost::PowerPlantArchetype& plant : _ships.powerPlants)
        {
          plant.powerCapacity = 0.0f;
        }
      },
      "the ship has no power plant");
  }
};

} // namespace GameLogicTests
