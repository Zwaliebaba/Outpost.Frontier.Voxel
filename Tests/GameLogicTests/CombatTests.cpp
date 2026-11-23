#include "Ai.h"
#include "Combat.h"
#include "FileSys.h"
#include "MaskEvent.h"
#include "Messages.h"
#include "SaveGame.h"
#include "ShipArchetype.h"
#include "TickClock.h"
#include "Universe.h"

#include "CppUnitTest.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

int g_moduleAnchor = 0;

constexpr std::uint32_t TICKS_PER_SECOND = Neuron::TickClock::TICKS_PER_SECOND;

[[nodiscard]] std::wstring Wide(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

[[nodiscard]] std::filesystem::path Assets()
{
  const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
  Assert::IsTrue(root.has_value(), L"Assets/Scene.txt above the test DLL");
  return *root;
}

/// The shipped universe with the player's Lancer at rest in space, _gap metres from the raider and
/// facing it: a duel about to begin.
void Duel(Outpost::Universe& _universe, double _gap)
{
  std::string error;
  Assert::IsTrue(_universe.LoadContent(Assets(), error), Wide(error).c_str());
  Assert::IsTrue(_universe.NewGame(error), Wide(error).c_str());
  const Outpost::ObjectId raider = _universe.FindByName("Raider");
  Assert::AreNotEqual(Outpost::NO_OBJECT, raider, L"slice 3's raider is in the scene");
  Outpost::SaveGame save = _universe.MakeSave();
  save.character.location = Outpost::CharacterLocation{};
  save.character.location.kind = Outpost::LocationKind::Space;
  Outpost::FlightState flight = *_universe.Flight(_universe.PlayerShip());
  const Outpost::UniversePosition& at = _universe.Flight(raider)->position;
  flight.position = Outpost::UniversePosition{at.x, at.y, at.z + _gap};
  flight.orientation = DirectX::XMFLOAT4{0.0f, 1.0f, 0.0f, 0.0f}; // turned about, towards the raider
  flight.velocityMetresPerSecond = DirectX::XMFLOAT3{};
  flight.angularVelocityRadiansPerSecond = DirectX::XMFLOAT3{};
  save.character.location.flight = flight;
  Assert::IsTrue(_universe.Restore(save, error), Wide(error).c_str());
}

/// The player's side of a scripted duel: the raider's own pilot record, on the player's ship,
/// attacking what is hostile. Its commands reach the server as a client's would.
class ScriptedPlayer
{
public:
  explicit ScriptedPlayer(const Outpost::Universe& _universe, std::uint64_t _seed)
  {
    std::string error;
    Outpost::ShipContent ships;
    Assert::IsTrue(Outpost::LoadShipContent(Assets() / "Content" / "Ships.txt", ships, error), Wide(error).c_str());
    m_spec = Outpost::FlightSpecFor(ships, *ships.FindShip("EscortLancer"));
    m_pilot = Outpost::Ai(*_universe.Battle().Content()->pilots.FindPilot("RaiderAce"), _seed);
  }

  void Fly(Outpost::Universe& _universe)
  {
    const Outpost::ObjectId player = _universe.PlayerShip();
    const Outpost::FlightState* own = _universe.Flight(player);
    if (own == nullptr || _universe.DockStatus(player).phase != Outpost::DockPhase::None)
    {
      return;
    }
    std::vector<Outpost::AiContact> contacts;
    const Outpost::ObjectId raider = _universe.FindByName("Raider");
    if (const Outpost::FlightState* enemy = _universe.Flight(raider))
    {
      contacts.push_back(
        Outpost::AiContact{raider, Outpost::Owner::Hostile, enemy->position, enemy->velocityMetresPerSecond, enemy->orientation, true});
    }
    Outpost::AiView view;
    view.own = own;
    view.spec = &m_spec;
    view.self = player;
    view.enemy = Outpost::Owner::Hostile;
    view.contacts = contacts;
    view.gunRangeMetres = 600.0f;
    view.muzzleSpeedMetresPerSecond = 600.0f;
    const Outpost::ShipCommand command = m_pilot.Think(view);
    static_cast<void>(_universe.SubmitCommand(Outpost::FlightInputMessage{_universe.TickCount() + 1, player, command}));
  }

private:
  Outpost::ShipFlightSpec m_spec;
  Outpost::Ai m_pilot;
};

/// Every event the server issued, applied by the one shared function to fresh initial masks, as a
/// client would apply them: each mask that still exists must hash as the server's (R22).
void AssertReplayMatches(const Outpost::Universe& _universe, const Outpost::SceneDefinition& _scene)
{
  const Outpost::Combat& combat = _universe.Battle();
  const Outpost::CombatContent& content = *combat.Content();
  std::vector<std::pair<Outpost::ObjectId, Neuron::VoxelMask>> replayed;
  std::size_t applied = 0;
  for (const Outpost::MaskEvent& event : combat.Events())
  {
    // Authored objects take identifiers in placement order (ADR-009), so the event's target names its instance.
    auto found = std::ranges::find(replayed, event.target, &std::pair<Outpost::ObjectId, Neuron::VoxelMask>::first);
    for (std::size_t i = 0; i < _scene.instances.size(); ++i)
    {
      if (static_cast<Outpost::ObjectId>(i + 1) == event.target)
      {
        const Outpost::VoxelArchetype* archetype = content.FindArchetype(_scene.instances[i].model);
        if (found == replayed.end() && archetype != nullptr)
        {
          replayed.emplace_back(event.target, Outpost::InitialMask(content, *archetype, _scene.instances[i].preset));
          found = replayed.end() - 1;
        }
        if (archetype != nullptr)
        {
          static_cast<void>(Outpost::ApplyMaskEvent(*archetype, event, found->second));
          ++applied;
        }
      }
    }
  }
  Assert::AreEqual(combat.Events().size(), applied, L"every event was replayed");
  std::size_t compared = 0;
  for (const auto& [id, mask] : replayed)
  {
    if (const Neuron::VoxelMask* server = combat.Mask(id))
    {
      Assert::AreEqual(server->Hash(), mask.Hash(), L"client and server masks hash equal");
      ++compared;
    }
  }
  Assert::IsTrue(compared > 0, L"something was carved and is still there to compare");
}

[[nodiscard]] Outpost::SceneDefinition ShippedScene()
{
  Outpost::SceneDefinition scene;
  std::string error;
  Assert::IsTrue(Outpost::LoadSceneContent(Assets(), scene, error), Wide(error).c_str());
  return scene;
}

} // namespace

TEST_CLASS(CombatTests)
{
public:
  TEST_METHOD(TheScriptedDuelKeepsTheBlueprintsRhythm)
  {
    // The exit criterion of plan slice 3, headless: the player's escort, flown by a scripted pilot,
    // against the raider of the same tier. The shield breaks in seconds, the rebuild window exposes
    // the hull, and the core is reached within two or three windows.
    Outpost::Universe universe;
    Duel(universe, 1500.0);
    ScriptedPlayer player(universe, 7);
    const Outpost::ObjectId raider = universe.FindByName("Raider");
    const float capacity = universe.Flight(raider)->shield;
    std::optional<std::uint64_t> firstShieldHit;
    std::optional<std::uint64_t> collapse;
    std::optional<std::uint64_t> coreReached;
    std::uint64_t exposedTicks = 0;
    std::uint64_t rebuildTicks = 0;
    for (std::uint64_t tick = 0; tick < 180ull * TICKS_PER_SECOND && universe.Flight(raider) != nullptr; ++tick)
    {
      player.Fly(universe);
      universe.Tick();
      const Outpost::FlightState* state = universe.Flight(raider);
      if (state == nullptr)
      {
        break;
      }
      if (!firstShieldHit.has_value() && state->shield < capacity)
      {
        firstShieldHit = universe.TickCount();
      }
      if (!collapse.has_value() && !state->shieldUp)
      {
        collapse = universe.TickCount();
        rebuildTicks = static_cast<std::uint64_t>(state->shieldRebuildTicks);
      }
      if (collapse.has_value() && universe.TickCount() < *collapse + rebuildTicks && !state->shieldUp)
      {
        ++exposedTicks;
      }
      if (!coreReached.has_value() && universe.Battle().CoreHealthPercent(raider) < 100)
      {
        coreReached = universe.TickCount();
        AssertReplayMatches(universe, ShippedScene()); // while the carved raider is still there to compare
      }
    }
    Assert::IsTrue(firstShieldHit.has_value() && collapse.has_value(), L"the raider's shield was hit and broke");
    const double toCollapse = static_cast<double>(*collapse - *firstShieldHit) / TICKS_PER_SECOND;
    Logger::WriteMessage(("first shield hit at " + std::to_string(*firstShieldHit) + ", collapse at " + std::to_string(*collapse) +
                          ", core reached at " + std::to_string(coreReached.value_or(0)) + "\n")
                           .c_str());
    Assert::IsTrue(toCollapse < 30.0, L"seconds to break a shield, not minutes");
    Assert::IsTrue(exposedTicks * 2 > rebuildTicks, L"the target is exposed for most of the rebuild window");
    Assert::IsTrue(coreReached.has_value() && *coreReached < *collapse + 3 * rebuildTicks, L"the core is reached within three windows");
    Assert::IsTrue(universe.Flight(raider) == nullptr || universe.PilotState(raider) == Outpost::AiState::Flee,
                   L"the beaten raider runs, or is gone");
  }

  TEST_METHOD(APlayerShotToItsCoreReturnsToBaseAsFittedThere)
  {
    // D1: back at the last base with the ship as it was fitted there; the ship that died breaks up.
    Outpost::Universe universe;
    Duel(universe, 1500.0);
    static_cast<void>(universe.TakeSaveRequest());
    const Outpost::ObjectId player = universe.PlayerShip();
    bool saved = false;
    std::optional<std::uint64_t> returned;
    for (std::uint64_t tick = 0; tick < 240ull * TICKS_PER_SECOND; ++tick)
    {
      universe.Tick(); // the player does nothing: the raider attacks a sitting ship
      saved = universe.TakeSaveRequest() || saved;
      if (universe.DockStatus(player).phase == Outpost::DockPhase::Docked)
      {
        returned = universe.TickCount();
        break;
      }
    }
    Assert::IsTrue(returned.has_value(), L"the raider got through to the core");
    Assert::AreEqual(std::string("Shipyard"), universe.DockStatus(player).station);
    const Outpost::ShipState* ship = universe.State().FindShip("Lancer");
    Assert::AreEqual(100, ship->hullPercent, L"as fitted at the base: the new game's ship");
    Assert::IsTrue(*universe.Battle().Mask(player) == universe.Battle().ArchetypeOf(player)->full);
    Assert::IsTrue(universe.Flight(player)->shieldUp && universe.Flight(player)->shield > 0.0f);
    Assert::IsFalse(universe.Battle().Wrecks().empty(), L"what died is left as wrecks");
    Assert::IsTrue(saved, L"saved once back in the berth (D17)");
    Assert::IsTrue(universe.Protected(player));
  }

  TEST_METHOD(ARepairKitPatchesStructureAndLeavesModules)
  {
    Outpost::Universe universe;
    Duel(universe, 1500.0);
    const Outpost::ObjectId player = universe.PlayerShip();
    Assert::AreEqual(std::int64_t{3}, universe.RepairKits(), L"System.txt's start record");
    Assert::IsFalse(universe.UseItem(Outpost::UseItemMessage{1, player, Outpost::ItemKind::RepairKit}), L"nothing to patch yet");
    Assert::AreEqual(std::int64_t{3}, universe.RepairKits(), L"and so not used up");
    while (universe.State().FindShip("Lancer")->hullPercent > 85 && universe.TickCount() < 200ull * TICKS_PER_SECOND)
    {
      universe.Tick();
    }
    const Outpost::ShipState before = *universe.State().FindShip("Lancer");
    Assert::IsTrue(before.hullPercent <= 85);
    const Neuron::VoxelMask damaged = *universe.Battle().Mask(player);
    Assert::IsTrue(universe.UseItem(Outpost::UseItemMessage{universe.TickCount(), player, Outpost::ItemKind::RepairKit}));
    Assert::AreEqual(std::int64_t{2}, universe.RepairKits());
    const Neuron::VoxelMask& patched = *universe.Battle().Mask(player);
    const Outpost::VoxelArchetype& lancer = *universe.Battle().ArchetypeOf(player);
    std::size_t restored = 0;
    for (std::uint32_t cell = 0; cell < patched.CellCount(); ++cell)
    {
      if (patched.PresentAt(cell) && !damaged.PresentAt(cell))
      {
        Assert::IsTrue(lancer.structure.PresentAt(cell), L"only structure comes back");
        ++restored;
      }
    }
    Assert::IsTrue(restored > 0 && restored <= Outpost::Universe::REPAIR_KIT_CELLS);
    Assert::IsTrue(universe.State().FindShip("Lancer")->hullPercent > before.hullPercent);
    Assert::IsTrue(universe.State().FindShip("Lancer")->modules == before.modules, L"modules are left for a hangar");
  }

  TEST_METHOD(APlayersBoltsStopOnWhatIsNotHostileAndAHostilesCarveIt)
  {
    // D24: a player's bolt that meets the yard stops and does nothing; a raider's carves it. D2: the
    // command block takes no damage from anyone.
    Outpost::Universe universe;
    std::string error;
    Assert::IsTrue(universe.LoadContent(Assets(), error), Wide(error).c_str());
    const Outpost::SceneDefinition scene = ShippedScene();
    Outpost::ShipContent ships;
    Assert::IsTrue(Outpost::LoadShipContent(Assets() / "Content" / "Ships.txt", ships, error));
    Outpost::Combat combat;
    combat.Load(std::shared_ptr<const Outpost::CombatContent>(universe.Battle().Content(), [](const Outpost::CombatContent*) {}), scene,
                universe.State().objects);
    std::vector<Outpost::CombatTarget> targets;
    for (const Outpost::UniverseObject& object : universe.State().objects)
    {
      targets.push_back(Outpost::CombatTarget{object.id, object.pose, object.scale, object.owner, false, nullptr, nullptr, 0.0f});
    }
    // A shot straight down onto the object, from 200 m above it.
    const auto shootAt = [&](const char* _instance, Outpost::Owner _owner)
    {
      const Outpost::UniverseObject* object = universe.State().FindObject(universe.FindByName(_instance));
      Outpost::Shot shot;
      shot.origin = Outpost::UniversePosition{object->pose.position.x, object->pose.position.y + 200.0, object->pose.position.z};
      shot.direction = DirectX::XMFLOAT3{0.0f, -1.0f, 0.0f};
      combat.Fire(999, _owner, shot, 0, ships, DirectX::XMFLOAT3{}, TICKS_PER_SECOND, 1);
      std::vector<Outpost::BoltHit> hits;
      for (int tick = 0; tick < 60 && hits.empty(); ++tick)
      {
        hits = combat.StepBolts(targets, ships, TICKS_PER_SECOND, 1);
      }
      Assert::AreEqual(std::size_t{1}, hits.size(), L"the bolt met something");
      return hits.front();
    };
    const Outpost::ObjectId tanks = universe.FindByName("TanksWest");
    const Neuron::VoxelMask before = *combat.Mask(tanks);
    const Outpost::BoltHit friendly = shootAt("TanksWest", Outpost::Owner::Player);
    Assert::AreEqual(tanks, friendly.target);
    Assert::IsFalse(friendly.harmful);
    Assert::IsTrue(*combat.Mask(tanks) == before, L"nothing carved");
    const Outpost::BoltHit hostile = shootAt("TanksWest", Outpost::Owner::Hostile);
    Assert::AreEqual(tanks, hostile.target);
    Assert::IsTrue(hostile.harmful && !hostile.carved.removed.empty(), L"a raider's bolt carves the yard");
    const Outpost::ObjectId command = universe.FindByName("Command");
    const Neuron::VoxelMask block = *combat.Mask(command);
    const Outpost::BoltHit indestructible = shootAt("Command", Outpost::Owner::Hostile);
    Assert::AreEqual(command, indestructible.target);
    Assert::IsFalse(indestructible.harmful);
    Assert::IsTrue(*combat.Mask(command) == block, L"D2: the command block cannot be destroyed");
  }

  TEST_METHOD(ABattleSurvivesASaveAndAResume)
  {
    Outpost::Universe universe;
    Duel(universe, 1500.0);
    ScriptedPlayer player(universe, 7);
    const Outpost::ObjectId raider = universe.FindByName("Raider");
    for (std::uint64_t tick = 0; tick < 180ull * TICKS_PER_SECOND && universe.Flight(raider) != nullptr; ++tick)
    {
      player.Fly(universe);
      universe.Tick();
    }
    Assert::IsTrue(universe.Flight(raider) == nullptr, L"the raider was destroyed or fled");
    Assert::IsTrue(std::ranges::find(universe.Gone(), std::string("Raider")) != universe.Gone().end());
    std::string text;
    std::string error;
    Assert::IsTrue(Outpost::FormatSaveGame(universe.MakeSave(), text, error), Wide(error).c_str());
    Outpost::SaveGame save;
    Assert::IsTrue(Outpost::ParseSaveGame(text, save, error), Wide(error).c_str());

    Outpost::Universe resumed;
    Assert::IsTrue(resumed.LoadContent(Assets(), error), Wide(error).c_str());
    Assert::IsTrue(resumed.Restore(save, error), Wide(error).c_str());
    Assert::AreEqual(Outpost::NO_OBJECT, resumed.FindByName("Raider"), L"a raider that is gone stays gone for that save");
    for (const Outpost::UniverseObject& object : universe.State().objects)
    {
      const Neuron::VoxelMask* mask = universe.Battle().Mask(object.id);
      const Neuron::VoxelMask* again = resumed.Battle().Mask(resumed.FindByName(object.name));
      Assert::IsTrue((mask == nullptr) == (again == nullptr));
      if (mask != nullptr)
      {
        Assert::AreEqual(mask->Hash(), again->Hash(), Wide(object.name).c_str());
      }
    }
    const Outpost::ShipState* lancer = resumed.State().FindShip("Lancer");
    Assert::IsTrue(*lancer == *universe.State().FindShip("Lancer") || lancer->object != universe.PlayerShip(),
                   L"the player's ship is measured again from the same mask");
    Assert::AreEqual(universe.RepairKits(), resumed.RepairKits());
    Assert::IsTrue(*resumed.Flight(resumed.PlayerShip()) == *universe.Flight(universe.PlayerShip()), L"its flight state, shield included");
  }
};

} // namespace GameLogicTests
