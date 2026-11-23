#include "ClientCombat.h"
#include "MaskEvent.h"
#include "MaskedSurfaces.h"
#include "Messages.h"
#include "Palette.h"
#include "VoxelMesher.h"

#include "CppUnitTest.h"

#include <algorithm>
#include <memory>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameAppTests
{

namespace
{

constexpr Outpost::ObjectId BLOCK = 5;

/// A 40-cell block of hull plating, its top layers in armour, as the one archetype of a content.
[[nodiscard]] std::shared_ptr<const Outpost::CombatContent> BlockContent()
{
  auto content = std::make_shared<Outpost::CombatContent>();
  Outpost::VoxelArchetype archetype;
  archetype.model = "Block";
  archetype.voxels = Neuron::VoxModel(40, 36, 34);
  for (int z = 0; z < 34; ++z)
  {
    for (int y = 0; y < 36; ++y)
    {
      for (int x = 0; x < 40; ++x)
      {
        archetype.voxels.Set(x, y, z, z > 30 ? Outpost::PaletteIndex::ARMOR_GRAY : Outpost::PaletteIndex::HULL_CHARCOAL);
      }
    }
  }
  archetype.full = Neuron::VoxelMask::FromModel(archetype.voxels);
  archetype.modules = Neuron::VoxelMask(archetype.full.Size());
  archetype.structure = archetype.full;
  content->archetypes.push_back(std::move(archetype));
  return content;
}

/// The client's universe with one object of the block, not placed by the scene: a wreck.
[[nodiscard]] Outpost::UniverseSnapshot BlockUniverse()
{
  Outpost::UniverseSnapshot universe;
  Outpost::UniverseObject object;
  object.id = BLOCK;
  object.name = "Wreck-5";
  object.model = "Block";
  universe.objects.push_back(object);
  return universe;
}

[[nodiscard]] Outpost::MaskSnapshotMessage WholeBlock(std::uint64_t _lastEvent)
{
  return Outpost::MaskSnapshotMessage{BLOCK, _lastEvent, Outpost::MaskReference::Full, {}};
}

[[nodiscard]] Outpost::DamageEventMessage CarveAt(std::uint64_t _sequence, Neuron::Int3 _cell)
{
  Outpost::DamageEventMessage damage;
  damage.event.target = BLOCK;
  damage.event.kind = Outpost::MaskEventKind::Carve;
  damage.event.carve =
    Neuron::CarveSpec{_sequence, _cell, {0, 0, -64}, Neuron::CarveShape::Capsule, 2, 6, 900, 77u + static_cast<std::uint32_t>(_sequence)};
  return damage;
}

void AssertSameVoxels(const Neuron::VoxelSurface& _expected, const Neuron::VoxelSurface& _actual)
{
  const auto key = [](const Neuron::SurfaceVoxel& _voxel) { return (_voxel.z * 256 + _voxel.y) * 256 + _voxel.x; };
  std::vector<Neuron::SurfaceVoxel> expected = _expected.voxels;
  std::vector<Neuron::SurfaceVoxel> actual = _actual.voxels;
  std::ranges::sort(expected, {}, key);
  std::ranges::sort(actual, {}, key);
  Assert::AreEqual(expected.size(), actual.size());
  for (std::size_t i = 0; i < expected.size(); ++i)
  {
    Assert::AreEqual(key(expected[i]), key(actual[i]));
    Assert::AreEqual(expected[i].colorIndex, actual[i].colorIndex);
    Assert::AreEqual(expected[i].exposedFaceMask, actual[i].exposedFaceMask);
    Assert::AreEqual(expected[i].faceOcclusion, actual[i].faceOcclusion);
  }
  Assert::AreEqual(_expected.solidCount, _actual.solidCount);
  Assert::IsTrue(_expected.boundsMin == _actual.boundsMin && _expected.boundsMax == _actual.boundsMax);
}

/// What the whole appearance extracts to in one piece, the masked surface's reference.
[[nodiscard]] Neuron::VoxelSurface WholeSurface(const Outpost::CombatContent& _content, const Neuron::VoxelMask& _mask)
{
  Neuron::VoxModel model = Outpost::MaskedSurfaces::BaseAppearance(_content.archetypes[0], nullptr);
  for (std::uint32_t i = 0; i < _mask.CellCount(); ++i)
  {
    if (!_mask.PresentAt(i))
    {
      const Neuron::Int3 at = _mask.CellOf(i);
      model.Set(at.x, at.y, at.z, Neuron::VOX_EMPTY);
    }
  }
  return Neuron::ExtractSurface(model, &Outpost::CanonicalPalette());
}

} // namespace

TEST_CLASS(ClientCombatTests)
{
public:
  TEST_METHOD(EventsAreAppliedInOrderAndOnlyOnce)
  {
    const auto content = BlockContent();
    Outpost::ClientCombat combat;
    combat.Load(content, Outpost::SceneDefinition{});
    const Outpost::UniverseSnapshot universe = BlockUniverse();
    Assert::IsTrue(combat.Apply(WholeBlock(0), universe));
    Assert::IsTrue(combat.Apply(CarveAt(1, {20, 18, 33}), universe));
    const std::uint32_t afterOne = combat.Find(BLOCK)->mask.PresentCount();
    Assert::IsTrue(afterOne < content->archetypes[0].full.PresentCount(), L"the carve took cells");
    Assert::IsTrue(combat.Apply(CarveAt(1, {20, 18, 33}), universe));
    Assert::AreEqual(afterOne, combat.Find(BLOCK)->mask.PresentCount(), L"an event already applied changes nothing");
    Assert::IsTrue(combat.Apply(CarveAt(3, {5, 5, 33}), universe));
    Assert::AreEqual(std::uint64_t{1}, combat.OutOfOrderEvents(), L"a gap is counted, not guessed at");

    // The same events through the shared function give the same mask (R22).
    Neuron::VoxelMask server = content->archetypes[0].full;
    static_cast<void>(Outpost::ApplyMaskEvent(content->archetypes[0], CarveAt(1, {20, 18, 33}).event, server));
    static_cast<void>(Outpost::ApplyMaskEvent(content->archetypes[0], CarveAt(3, {5, 5, 33}).event, server));
    Assert::AreEqual(server.Hash(), combat.Find(BLOCK)->mask.Hash());
    Assert::IsFalse(combat.Apply(Outpost::TickMessage{4}, universe), L"not a combat message");
  }

  TEST_METHOD(ASnapshotAgainstNothingHoldsOnlyItsSpans)
  {
    const auto content = BlockContent();
    Outpost::ClientCombat combat;
    combat.Load(content, Outpost::SceneDefinition{});
    const Outpost::UniverseSnapshot universe = BlockUniverse();
    Assert::IsTrue(combat.Apply(Outpost::MaskSnapshotMessage{BLOCK, 9, Outpost::MaskReference::Empty, {{10, 4}, {200, 2}}}, universe));
    const Outpost::ClientMask* held = combat.Find(BLOCK);
    Assert::IsNotNull(held);
    Assert::AreEqual(std::uint32_t{6}, held->mask.PresentCount());
    Assert::AreEqual(std::uint64_t{9}, held->mask.LastEvent());
    Assert::IsTrue(held->whole);
    // A snapshot that does not fit the grid is not taken.
    Assert::IsTrue(combat.Apply(Outpost::MaskSnapshotMessage{BLOCK, 10, Outpost::MaskReference::Empty, {{48'950, 100}}}, universe));
    Assert::AreEqual(std::uint64_t{9}, combat.Find(BLOCK)->mask.LastEvent());
  }

  TEST_METHOD(BoltsFlyUntilTheyHitOrExpire)
  {
    Outpost::ClientCombat combat;
    combat.Load(BlockContent(), Outpost::SceneDefinition{});
    Outpost::UniverseSnapshot universe = BlockUniverse();
    Assert::IsTrue(combat.Apply(Outpost::BoltFiredMessage{10, 1, 7, {0.0, 0.0, 0.0}, {0.0f, 0.0f, 600.0f}, 60}, universe));
    Assert::IsTrue(combat.Apply(Outpost::BoltFiredMessage{10, 2, 7, {0.0, 0.0, 0.0}, {600.0f, 0.0f, 0.0f}, 60}, universe));
    Assert::AreEqual(300.0, combat.Bolts().front().At(40.0, 60).z, 1e-9);
    Assert::IsTrue(combat.Apply(Outpost::BoltHitMessage{11, 1, BLOCK, {0.0, 0.0, 10.0}, false, true}, universe));
    Assert::AreEqual(std::size_t{1}, combat.Bolts().size(), L"a bolt that hit is gone");
    Assert::AreEqual(std::size_t{1}, combat.TakeHits().size());
    Assert::IsTrue(combat.TakeHits().empty(), L"a hit is shown once");
    universe.tick = 71;
    combat.Prune(universe);
    Assert::IsTrue(combat.Bolts().empty(), L"and one past its lifetime");
    universe.objects.clear();
    Assert::IsTrue(combat.Apply(WholeBlock(0), BlockUniverse()));
    combat.Prune(universe);
    Assert::IsNull(combat.Find(BLOCK), L"an object that is gone takes its mask with it");
  }

  TEST_METHOD(BricksBuildTheWholeSurfaceWithinTheirBudget)
  {
    const auto content = BlockContent();
    Outpost::ClientCombat combat;
    combat.Load(content, Outpost::SceneDefinition{});
    const Outpost::UniverseSnapshot universe = BlockUniverse();
    Assert::IsTrue(combat.Apply(WholeBlock(0), universe));
    Outpost::MaskedSurfaces surfaces;
    surfaces.Load(content, {nullptr});
    // 40 x 36 x 34 cells are 3 x 3 x 3 bricks: two frames at twenty a frame.
    Assert::AreEqual(std::size_t{20}, surfaces.Update(combat, {}));
    Assert::IsFalse(surfaces.Find(BLOCK)->ready, L"nothing is drawn from half the bricks");
    Assert::AreEqual(std::size_t{7}, surfaces.Update(combat, {}));
    const Outpost::MaskedSurface* drawn = surfaces.Find(BLOCK);
    Assert::IsTrue(drawn->ready);
    Assert::AreEqual(std::uint64_t{1}, drawn->generation);
    AssertSameVoxels(WholeSurface(*content, combat.Find(BLOCK)->mask), drawn->surface);
    Assert::AreEqual(std::size_t{0}, surfaces.Update(combat, {}), L"nothing waits: nothing is extracted");
    Assert::AreEqual(std::uint64_t{1}, surfaces.Find(BLOCK)->generation);
  }

  TEST_METHOD(AnEventExtractsOnlyTheBricksItTouches)
  {
    const auto content = BlockContent();
    Outpost::ClientCombat combat;
    combat.Load(content, Outpost::SceneDefinition{});
    const Outpost::UniverseSnapshot universe = BlockUniverse();
    Assert::IsTrue(combat.Apply(WholeBlock(0), universe));
    Outpost::MaskedSurfaces surfaces;
    surfaces.Load(content, {nullptr});
    static_cast<void>(surfaces.Update(combat, {}, 100));
    // Down through the middle of a column of bricks, well inside it across: the top brick and the
    // one under it, and none beside them.
    Assert::IsTrue(combat.Apply(CarveAt(1, {24, 24, 33}), universe));
    Assert::AreEqual(std::size_t{2}, surfaces.Update(combat, {}, 100));
    Assert::AreEqual(std::uint64_t{2}, surfaces.Find(BLOCK)->generation);
    AssertSameVoxels(WholeSurface(*content, combat.Find(BLOCK)->mask), surfaces.Find(BLOCK)->surface);
    // On a corner where bricks meet: the neighbours that read its cells go too.
    Assert::IsTrue(combat.Apply(CarveAt(2, {16, 16, 33}), universe));
    Assert::IsTrue(surfaces.Update(combat, {}, 100) >= 4);
    AssertSameVoxels(WholeSurface(*content, combat.Find(BLOCK)->mask), surfaces.Find(BLOCK)->surface);
  }

  TEST_METHOD(TheSceneModelsColorsShowWhereItHasCells)
  {
    const auto content = BlockContent();
    Neuron::VoxModel scene(40, 36, 34);
    scene.Set(3, 3, 33, Outpost::PaletteIndex::SAFETY_ORANGE);
    scene.Set(3, 4, 33, Outpost::PaletteIndex::SAFETY_ORANGE);
    const Neuron::VoxModel base = Outpost::MaskedSurfaces::BaseAppearance(content->archetypes[0], &scene);
    Assert::AreEqual(Outpost::PaletteIndex::SAFETY_ORANGE, base.At(3, 3, 33));
    Assert::AreEqual(Outpost::PaletteIndex::ARMOR_GRAY, base.At(5, 3, 33), L"elsewhere, the archetype's own");
    const Neuron::VoxModel other(4, 4, 4);
    Assert::AreEqual(Outpost::PaletteIndex::ARMOR_GRAY,
                     Outpost::MaskedSurfaces::BaseAppearance(content->archetypes[0], &other).At(3, 3, 33),
                     L"a model of another grid is not overlaid");
  }
};

} // namespace GameAppTests
