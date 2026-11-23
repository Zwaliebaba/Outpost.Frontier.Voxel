#include "FlightModel.h"
#include "Launcher.h"
#include "MaskEvent.h"
#include "ModuleRegion.h"
#include "Palette.h"
#include "SceneDefinition.h"
#include "Shield.h"
#include "ShipArchetype.h"
#include "ShipFlight.h"
#include "TestSupport.h"
#include "VoxFile.h"

#include "CppUnitTest.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

constexpr std::uint32_t TICKS_PER_SECOND = 60;

[[nodiscard]] std::wstring Wide(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// The shipped content, with every mount's muzzle resolved: what both halves load.
struct Shipped
{
  Outpost::SceneDefinition scene;
  Outpost::ShipContent ships;
  Outpost::HullContent hulls;
};

[[nodiscard]] const Shipped& LoadShipped()
{
  static const Shipped SHIPPED = []
  {
    Shipped shipped;
    std::string error;
    Assert::IsTrue(Outpost::LoadSceneContent(AssetRoot(), shipped.scene, error), Wide(error).c_str());
    Assert::IsTrue(Outpost::LoadShipContent(AssetRoot() / "Content" / "Ships.txt", shipped.ships, error), Wide(error).c_str());
    Assert::IsTrue(Outpost::LoadHullContent(AssetRoot(), shipped.hulls, error), Wide(error).c_str());
    const std::vector<std::string> problems = Outpost::ResolveMounts(shipped.ships, shipped.scene, shipped.hulls);
    Assert::IsTrue(problems.empty(), problems.empty() ? L"" : Wide(problems.front()).c_str());
    return shipped;
  }();
  return SHIPPED;
}

/// The Lancer's spec, shield and guns included, at rest at the origin facing +Z.
[[nodiscard]] Outpost::ShipFlightSpec LancerSpec()
{
  const Shipped& shipped = LoadShipped();
  return Outpost::FlightSpecFor(shipped.ships, *shipped.ships.FindShip("EscortLancer"));
}

} // namespace

TEST_CLASS(LauncherTests)
{
public:
  TEST_METHOD(TheEscortsCarryTwoGunsWithMuzzlesAtTheFrontOfTheirWeaponModules)
  {
    const Outpost::ShipFlightSpec spec = LancerSpec();
    Assert::IsTrue(spec.launchers[0].fitted && spec.launchers[1].fitted && !spec.launchers[2].fitted);
    // WeaponPort is VOX x 3..8, forward face y = 35, z 3..10, about the 28 x 52 x 18 grid's centre.
    Assert::IsTrue(Near(spec.launchers[0].muzzleModel, DirectX::XMFLOAT3{5.5f - 14.0f, 6.5f - 9.0f, 35.0f - 26.0f}));
    Assert::IsTrue(spec.launchers[1].muzzleModel.x > 0.0f, L"the starboard gun is on the right");
    Assert::AreEqual(450.0f, spec.shield.capacity);
  }

  TEST_METHOD(EachRefusalComesInTheBlueprintsOrder)
  {
    Outpost::ShipFlightSpec spec = LancerSpec();
    Outpost::FlightState state = Outpost::RestingFlightState(spec, Outpost::Pose{});
    const Outpost::UniversePosition ahead{0.0, 0.0, 400.0};

    Outpost::FireResult result = Outpost::StepLaunchers(spec, true, ahead, TICKS_PER_SECOND, state);
    Assert::IsTrue(result.outcomes[0] == Outpost::FireOutcome::Fired && result.outcomes[1] == Outpost::FireOutcome::Fired);
    Assert::AreEqual(std::uint8_t{2}, result.shotCount);
    Assert::IsTrue(result.outcomes[2] == Outpost::FireOutcome::Idle, L"no third gun");
    result = Outpost::StepLaunchers(spec, true, ahead, TICKS_PER_SECOND, state);
    Assert::IsTrue(result.outcomes[0] == Outpost::FireOutcome::Refire, L"0.2 s between shots: 12 ticks");

    // Test 1 before test 2: a stopped module disables its gun whatever its clock says.
    spec.launchers[0].working = false;
    state.launcherClockTicks[0] = Outpost::LAUNCHER_CLOCK_READY_TICKS;
    Assert::IsTrue(Outpost::StepLaunchers(spec, true, ahead, TICKS_PER_SECOND, state).outcomes[0] == Outpost::FireOutcome::Disabled);
    spec.launchers[0].working = true;

    // Test 3, and the clock restarts on an attempt that passed test 2.
    state.launcherClockTicks[0] = Outpost::LAUNCHER_CLOCK_READY_TICKS;
    state.powerPool = 1.0f;
    Assert::IsTrue(Outpost::StepLaunchers(spec, true, ahead, TICKS_PER_SECOND, state).outcomes[0] == Outpost::FireOutcome::Power);
    Assert::AreEqual(0, state.launcherClockTicks[0], L"trying on an empty pool still costs a cycle");
    state.powerPool = 1000.0f;
    Assert::IsTrue(Outpost::StepLaunchers(spec, true, ahead, TICKS_PER_SECOND, state).outcomes[0] == Outpost::FireOutcome::Refire);

    // Test 7: an aim point behind the ship.
    state.launcherClockTicks[0] = Outpost::LAUNCHER_CLOCK_READY_TICKS;
    Assert::IsTrue(Outpost::StepLaunchers(spec, true, Outpost::UniversePosition{0.0, 0.0, -400.0}, TICKS_PER_SECOND, state).outcomes[0] ==
                   Outpost::FireOutcome::AimLimit);
    // Test 8: cruise.
    state.launcherClockTicks[0] = Outpost::LAUNCHER_CLOCK_READY_TICKS;
    state.cruise = Outpost::CruiseState::Cruising;
    Assert::IsTrue(Outpost::StepLaunchers(spec, true, ahead, TICKS_PER_SECOND, state).outcomes[0] == Outpost::FireOutcome::Cruise);
  }

  TEST_METHOD(AShotCostsPowerAndConvergesOnTheAimPoint)
  {
    const Outpost::ShipFlightSpec spec = LancerSpec();
    Outpost::FlightState state = Outpost::RestingFlightState(spec, Outpost::Pose{});
    const Outpost::UniversePosition aim{0.0, 0.0, 300.0};
    const Outpost::FireResult result = Outpost::StepLaunchers(spec, true, aim, TICKS_PER_SECOND, state);
    Assert::AreEqual(1000.0f - 2.0f * 8.5f, state.powerPool, 1e-3f);
    for (std::size_t i = 0; i < result.shotCount; ++i)
    {
      const Outpost::Shot& shot = result.shots[i];
      const DirectX::XMVECTOR toAim = DirectX::XMVector3Normalize(DirectX::XMVectorSet(static_cast<float>(aim.x - shot.origin.x),
                                                                                       static_cast<float>(aim.y - shot.origin.y),
                                                                                       static_cast<float>(aim.z - shot.origin.z), 0.0f));
      Assert::AreEqual(1.0f, DirectX::XMVectorGetX(DirectX::XMVector3Dot(toAim, DirectX::XMLoadFloat3(&shot.direction))), 1e-5f);
    }
    Assert::IsTrue(result.shots[0].direction.x > 0.0f && result.shots[1].direction.x < 0.0f, L"the guns converge from either side");
  }

  TEST_METHOD(TheLeadPointMeetsACrossingTarget)
  {
    // A target 600 m ahead crossing at 100 m/s, a 600 m/s bolt: the bolt and the target meet.
    const DirectX::XMFLOAT3 offset{0.0f, 0.0f, 600.0f};
    const DirectX::XMFLOAT3 velocity{100.0f, 0.0f, 0.0f};
    const std::optional<float> t = Outpost::LeadSeconds(offset, velocity, 600.0f);
    Assert::IsTrue(t.has_value());
    const float tx = offset.x + velocity.x * *t;
    const float tz = offset.z + velocity.z * *t;
    Assert::AreEqual(600.0f * *t, std::sqrt(tx * tx + tz * tz), 1e-2f, L"the bolt has flown as far as the target is when they meet");
    Assert::IsFalse(Outpost::LeadSeconds(offset, DirectX::XMFLOAT3{0.0f, 0.0f, 900.0f}, 600.0f).has_value(),
                    L"a target outrunning the bolt");
  }

  TEST_METHOD(FiringStaysInTheSharedTickSoPredictionPaysTheSame)
  {
    const Shipped& shipped = LoadShipped();
    Outpost::ShipBody body;
    body.object = 1;
    body.spec = LancerSpec();
    body.radiusMetres = 30.0f;
    Outpost::FlightState server = Outpost::RestingFlightState(body.spec, Outpost::Pose{});
    Outpost::FlightState client = server;
    Outpost::ShipCommand command;
    command.wishes.fire = true;
    command.wishes.throttle = 1.0f;
    command.aim = Outpost::UniversePosition{0.0, 0.0, 500.0};
    int shots = 0;
    for (int tick = 0; tick < 120; ++tick)
    {
      const Outpost::ShipTickResult a = Outpost::StepShip(body, command, {}, TICKS_PER_SECOND, server);
      const Outpost::ShipTickResult b = Outpost::StepShip(body, command, {}, TICKS_PER_SECOND, client);
      Assert::AreEqual(a.fire.shotCount, b.fire.shotCount);
      Assert::IsTrue(server == client, L"tick for tick, bit for bit");
      shots += a.fire.shotCount;
    }
    Assert::AreEqual(20, shots, L"two guns, a shot every 12 ticks, for two seconds");
    static_cast<void>(shipped);
  }
};

TEST_CLASS(ShieldTests)
{
public:
  TEST_METHOD(TheStateTableOfChapterThree)
  {
    const Outpost::ShipFlightSpec spec = LancerSpec();
    Outpost::FlightState state = Outpost::RestingFlightState(spec, Outpost::Pose{});
    Assert::IsTrue(state.shieldUp && state.shield == 450.0f);
    // Up, at or below 15 %: down, for the rebuild time.
    Assert::IsFalse(Outpost::DamageShield(spec, TICKS_PER_SECOND, 300.0f, state));
    Assert::IsTrue(Outpost::DamageShield(spec, TICKS_PER_SECOND, 100.0f, state), L"50 left is under 67.5");
    Assert::IsFalse(state.shieldUp);
    Assert::AreEqual(12 * 60, state.shieldRebuildTicks);
    // It regenerates while down, drawing from the pool, and comes back once the rebuild time is over.
    for (int tick = 0; tick < 12 * 60 - 1; ++tick)
    {
      Outpost::StepShield(spec, TICKS_PER_SECOND, state);
    }
    Assert::IsFalse(state.shieldUp, L"one tick short of the rebuild time");
    Outpost::StepShield(spec, TICKS_PER_SECOND, state);
    Assert::IsTrue(state.shieldUp);
    Assert::AreEqual(50.0f + 12.0f * 10.0f, state.shield, 0.1f);
    Assert::AreEqual(1000.0f - 12.0f * 10.0f, state.powerPool, 0.1f, L"the draw came from the pool");
  }

  TEST_METHOD(NothingPassesThroughAndADrainedPoolStopsRecovery)
  {
    const Outpost::ShipFlightSpec spec = LancerSpec();
    Outpost::FlightState state = Outpost::RestingFlightState(spec, Outpost::Pose{});
    static_cast<void>(Outpost::DamageShield(spec, TICKS_PER_SECOND, 5000.0f, state));
    Assert::AreEqual(0.0f, state.shield, L"a hit that empties the shield is absorbed whole");
    state.powerPool = 0.0f;
    Outpost::StepShield(spec, TICKS_PER_SECOND, state);
    Assert::AreEqual(0.0f, state.shield, L"no power, no regeneration");
  }

  TEST_METHOD(TheEscortsShieldBreaksInSecondsUnderTwoGuns)
  {
    // The calibration target (GameDesign.md §7.1): seconds to break a shield. Each hit of the light
    // gun takes half its 28 damage; the shield of 450 collapses at 67.5.
    const float perHit = Outpost::ShieldDamage(28.0f, 0.0f);
    Assert::AreEqual(14.0f, perHit);
    const int hits = static_cast<int>(std::ceil((450.0f - 0.15f * 450.0f) / perHit));
    Assert::AreEqual(28, hits);
    // Two guns at five shots a second, every shot landing: under four seconds; half landing, under eight.
    Assert::IsTrue(static_cast<float>(hits) / 10.0f < 4.0f && static_cast<float>(hits) / 5.0f < 8.0f);
  }
};

TEST_CLASS(ModuleRegionTests)
{
public:
  TEST_METHOD(EveryHullHasOneCoreAndRegionsInsideItsMaster)
  {
    const Shipped& shipped = LoadShipped();
    Assert::AreEqual(std::size_t{5}, shipped.hulls.hulls.size());
    for (const Outpost::HullRecord& hull : shipped.hulls.hulls)
    {
      Assert::IsNotNull(hull.Core());
      Neuron::VoxDocument master;
      Assert::IsTrue(Neuron::LoadVoxFile(AssetRoot() / "Models" / (hull.master + ".vox"), master) == Neuron::VoxError::None);
      Assert::IsTrue(static_cast<int>(master.model.SizeX()) == hull.size.x && static_cast<int>(master.model.SizeY()) == hull.size.y &&
                     static_cast<int>(master.model.SizeZ()) == hull.size.z);
      const Neuron::VoxelMask full = Neuron::VoxelMask::FromModel(master.model);
      for (const Outpost::ModuleRegion& region : hull.regions)
      {
        const std::array<Outpost::ModuleFunction, 1> only{region.function};
        Outpost::HullRecord single = hull;
        single.regions = {region};
        Assert::IsTrue(Outpost::RegionCells(single, full, only).PresentCount() > 0, Wide(hull.name + "." + region.name).c_str());
      }
      Assert::IsNotNull(shipped.scene.FindModel(hull.legacy), L"every hull is placed by a scene model");
    }
  }

  TEST_METHOD(TheFrigatesPresetReproducesItsSceneModelExactly)
  {
    const Shipped& shipped = LoadShipped();
    const Outpost::DamagePreset* preset = shipped.hulls.FindPreset("FrigateF317");
    Assert::IsNotNull(preset);
    Neuron::VoxDocument master;
    Neuron::VoxDocument legacy;
    Assert::IsTrue(Neuron::LoadVoxFile(AssetRoot() / "Models" / "FrigateIntact.vox", master) == Neuron::VoxError::None);
    Assert::IsTrue(Neuron::LoadVoxFile(AssetRoot() / "Models" / "Frigate.vox", legacy) == Neuron::VoxError::None);
    Neuron::VoxelMask damaged;
    Assert::IsTrue(Neuron::MaskFromAbsentSpans(Neuron::VoxelMask::FromModel(master.model), preset->removed, damaged));
    Assert::AreEqual(81321u - 5063u, damaged.PresentCount(), L"ADR-021: 5,063 cells removed");
    Assert::IsTrue(damaged == Neuron::VoxelMask::FromModel(legacy.model), L"the scene's frigate, cell for cell");
  }

  TEST_METHOD(ThresholdsWeakenThenStopAFunction)
  {
    const Shipped& shipped = LoadShipped();
    const Outpost::FunctionThreshold engine = shipped.hulls.ThresholdFor(Outpost::ModuleFunction::Engine);
    Assert::AreEqual(100, Outpost::RatePercent(engine, 60));
    Assert::AreEqual(50, Outpost::RatePercent(engine, 59));
    Assert::AreEqual(50, Outpost::RatePercent(engine, 20));
    Assert::AreEqual(0, Outpost::RatePercent(engine, 19));
    Assert::AreEqual(0, Outpost::RatePercent(Outpost::FunctionThreshold{Outpost::ModuleFunction::Sensor, 0, 0, 100}, 0),
                     L"a module with no cells has stopped, whatever its thresholds");
    Assert::AreEqual(33, Outpost::HealthPercent(1, 3), L"health rounds down");
  }

  TEST_METHOD(AnEngineCarvedAwayStopsThrustAndAWeaponModuleItsGun)
  {
    const Shipped& shipped = LoadShipped();
    const Outpost::HullRecord& hull = *shipped.hulls.FindHull("EscortLancer");
    const std::vector<std::string> mounts = shipped.ships.MountRegions("EscortLancer");
    std::vector<Outpost::ShipModuleState> modules{{"EnginePort", 10}, {"EngineStarboard", 100}, {"WeaponPort", 40}};
    Outpost::ShipCondition condition = Outpost::ConditionOf(shipped.hulls, hull, modules, mounts);
    Assert::AreEqual(50, condition.engineRatePercent, L"one engine of two");
    Assert::IsFalse(condition.mountWorking[0]);
    Assert::IsTrue(condition.mountWorking[1]);
    modules[1].healthPercent = 0;
    condition = Outpost::ConditionOf(shipped.hulls, hull, modules, mounts);
    Assert::AreEqual(0, condition.engineRatePercent, L"no engine cells, no thrust");
    const Outpost::ShipFlightSpec spec = Outpost::ApplyCondition(LancerSpec(), condition);
    Assert::AreEqual(0.0f, spec.engineThrustNewtons);
    Assert::IsFalse(spec.launchers[0].working);
    modules.push_back({"Core", 49});
    Assert::IsTrue(Outpost::ConditionOf(shipped.hulls, hull, modules, mounts).coreStopped, L"the core stops below half");
  }

  TEST_METHOD(BrokenHullContentIsRefusedWithTheLine)
  {
    Outpost::HullContent content;
    std::string error;
    Assert::IsFalse(Outpost::ParseHulls("hull name=H model=M legacy=L size=4,4,4\n", "H.txt", content, error));
    Assert::IsTrue(error.find("core regions") != std::string::npos, Wide(error).c_str());
    Assert::IsFalse(Outpost::ParseHulls(
      "hull name=H model=M legacy=L size=4,4,4\nregion hull=H name=Core function=core min=0,0,0 max=5,1,1\n", "H.txt", content, error));
    Assert::IsTrue(error.find("line 2") != std::string::npos && error.find("leaves") != std::string::npos, Wide(error).c_str());
    Assert::IsFalse(Outpost::ParseModuleThresholds("function name=engine reduced=20 stopped=60 reducedrate=50\n", "M.txt", content, error));
  }
};

TEST_CLASS(MaskEventTests)
{
public:
  TEST_METHOD(EveryColorAShippedModelUsesIsAMaterialACarveCanRemove)
  {
    for (const auto& entry : std::filesystem::directory_iterator(AssetRoot() / "Models"))
    {
      Neuron::VoxDocument document;
      Assert::IsTrue(Neuron::LoadVoxFile(entry.path(), document) == Neuron::VoxError::None);
      for (int z = 0; z < static_cast<int>(document.model.SizeZ()); ++z)
      {
        for (int y = 0; y < static_cast<int>(document.model.SizeY()); ++y)
        {
          for (int x = 0; x < static_cast<int>(document.model.SizeX()); ++x)
          {
            const std::uint8_t color = document.model.At(x, y, z);
            if (color != Neuron::VOX_EMPTY)
            {
              const std::uint32_t strength = Outpost::MaterialOf(color).strength;
              Assert::IsTrue(strength > 0 && strength < Neuron::UNBREAKABLE);
            }
          }
        }
      }
    }
    Assert::IsTrue(Outpost::MaterialOf(Outpost::PaletteIndex::ARMOR_GRAY).strength >
                     Outpost::MaterialOf(Outpost::PaletteIndex::HULL_CHARCOAL).strength,
                   L"armour is a stronger outer material, not a divisor");
    Assert::AreEqual(1u, Outpost::MaterialOf(Outpost::PaletteIndex::ORE).oreYield);
  }

  TEST_METHOD(BothHalvesApplyingTheSameEventsHoldTheSameMask)
  {
    std::string error;
    Outpost::SceneDefinition scene;
    Assert::IsTrue(Outpost::LoadSceneContent(AssetRoot(), scene, error), Wide(error).c_str());
    Outpost::CombatContent content;
    Assert::IsTrue(Outpost::LoadCombatContent(AssetRoot(), scene, content, error), Wide(error).c_str());
    const Outpost::VoxelArchetype* lancer = content.FindArchetype("EscortLancer");
    Assert::IsNotNull(lancer);
    Assert::AreEqual(std::size_t{48}, lancer->coreCells.size());
    Neuron::VoxelMask server = Outpost::InitialMask(content, *lancer, "");
    Neuron::VoxelMask client = server;
    std::size_t detached = 0;
    for (std::uint32_t i = 1; i <= 60; ++i)
    {
      Outpost::MaskEvent event;
      event.carve.sequence = i;
      event.carve.cell = Neuron::Int3{static_cast<int>(i % 28), 51 - static_cast<int>(i % 20), static_cast<int>(i % 18)};
      event.carve.direction = Neuron::Int3{0, -64, 0};
      event.carve.radiusCells = 2;
      event.carve.energy = 300;
      event.carve.seed = i * 7919u;
      event.toughened = (i % 2) == 0;
      const Outpost::MaskEventResult a = Outpost::ApplyMaskEvent(*lancer, event, server);
      const Outpost::MaskEventResult b = Outpost::ApplyMaskEvent(*lancer, event, client);
      Assert::IsTrue(a.removed == b.removed && a.detached == b.detached);
      detached += a.detached.size();
      Assert::AreEqual(server.Hash(), client.Hash());
    }
    Assert::IsTrue(server.PresentCount() < lancer->full.PresentCount());
    // A patch restores structure only, never a module cell.
    Outpost::MaskEvent patch;
    patch.kind = Outpost::MaskEventKind::Patch;
    patch.patch = Neuron::PatchSpec{61, 500};
    const Outpost::MaskEventResult restored = Outpost::ApplyMaskEvent(*lancer, patch, server);
    Assert::IsTrue(!restored.restored.empty());
    for (const std::uint32_t cell : restored.restored)
    {
      Assert::IsTrue(lancer->structure.PresentAt(cell) && !lancer->modules.PresentAt(cell));
    }
    static_cast<void>(detached);
  }

  TEST_METHOD(APlayersModulesAreTougherThanAnNpcsInTheSameHull)
  {
    std::string error;
    Outpost::SceneDefinition scene;
    Assert::IsTrue(Outpost::LoadSceneContent(AssetRoot(), scene, error), Wide(error).c_str());
    Outpost::CombatContent content;
    Assert::IsTrue(Outpost::LoadCombatContent(AssetRoot(), scene, content, error), Wide(error).c_str());
    const Outpost::VoxelArchetype& lancer = *content.FindArchetype("EscortLancer");
    // The same shot at the port weapon module's face, on an NPC's hull and on a player's.
    Outpost::MaskEvent event;
    event.carve.sequence = 1;
    event.carve.cell = Neuron::Int3{5, 34, 6};
    event.carve.radiusCells = 2;
    event.carve.energy = 400;
    Neuron::VoxelMask npc = lancer.full;
    Neuron::VoxelMask player = lancer.full;
    const std::size_t npcCells = Outpost::ApplyMaskEvent(lancer, event, npc).removed.size();
    event.toughened = true;
    const std::size_t playerCells = Outpost::ApplyMaskEvent(lancer, event, player).removed.size();
    Assert::IsTrue(playerCells < npcCells, L"the same energy takes fewer of a player's module cells");
  }
};

} // namespace GameCoreTests
