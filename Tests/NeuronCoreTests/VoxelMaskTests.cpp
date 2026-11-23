#include "FileSys.h"
#include "VoxFile.h"
#include "VoxelCarve.h"
#include "VoxelMask.h"

#include "CppUnitTest.h"

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

int g_moduleAnchor = 0;

/// A solid block of one colour.
[[nodiscard]] Neuron::VoxModel Block(int _x, int _y, int _z, std::uint8_t _color = 1)
{
  Neuron::VoxModel model(static_cast<std::uint32_t>(_x), static_cast<std::uint32_t>(_y), static_cast<std::uint32_t>(_z));
  for (int z = 0; z < _z; ++z)
  {
    for (int y = 0; y < _y; ++y)
    {
      for (int x = 0; x < _x; ++x)
      {
        model.Set(x, y, z, _color);
      }
    }
  }
  return model;
}

/// Every colour costs _strength, except colour 2, which nothing removes.
[[nodiscard]] Neuron::StrengthTable Strengths(std::uint32_t _strength)
{
  Neuron::StrengthTable table{};
  table.fill(_strength);
  table[2] = Neuron::UNBREAKABLE;
  return table;
}

[[nodiscard]] Neuron::CarveSpec Shot(int _x, int _y, int _z, std::uint8_t _radius, std::uint32_t _energy, std::uint32_t _seed = 7)
{
  Neuron::CarveSpec spec;
  spec.sequence = 1;
  spec.cell = Neuron::Int3{_x, _y, _z};
  spec.radiusCells = _radius;
  spec.energy = _energy;
  spec.seed = _seed;
  return spec;
}

[[nodiscard]] Neuron::VoxModel LoadModel(const char* _file)
{
  const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
  Assert::IsTrue(root.has_value(), L"Assets/Scene.txt above the test DLL");
  Neuron::VoxDocument document;
  Assert::IsTrue(Neuron::LoadVoxFile(*root / "Models" / _file, document) == Neuron::VoxError::None);
  return document.model;
}

} // namespace

TEST_CLASS(VoxelMaskTests)
{
public:
  TEST_METHOD(AMaskFromAModelHoldsItsSolidCells)
  {
    Neuron::VoxModel model(4, 3, 2);
    model.Set(0, 0, 0, 5);
    model.Set(3, 2, 1, 9);
    const Neuron::VoxelMask mask = Neuron::VoxelMask::FromModel(model);
    Assert::AreEqual(24u, mask.CellCount());
    Assert::AreEqual(2u, mask.PresentCount());
    Assert::IsTrue(mask.Present(0, 0, 0));
    Assert::IsTrue(mask.Present(3, 2, 1));
    Assert::IsFalse(mask.Present(1, 0, 0));
    Assert::IsFalse(mask.Present(-1, 0, 0), L"outside the grid is absent");
    Assert::AreEqual(23u, mask.Index(3, 2, 1), L"x + sizeX * (y + sizeY * z), as the hull fixtures index cells");
    Assert::IsTrue(mask.CellOf(23) == Neuron::Int3{3, 2, 1});
  }

  TEST_METHOD(TheHashSeesEveryCellAndOnlyCells)
  {
    Neuron::VoxelMask a = Neuron::VoxelMask::FromModel(Block(5, 5, 5));
    Neuron::VoxelMask b = a;
    Assert::AreEqual(a.Hash(), b.Hash());
    b.SetLastEvent(99);
    Assert::AreEqual(a.Hash(), b.Hash(), L"the event number is not part of what the mask holds");
    b.SetAt(62, false);
    Assert::AreNotEqual(a.Hash(), b.Hash());
    b.SetAt(62, true);
    Assert::AreEqual(a.Hash(), b.Hash());
    Assert::AreNotEqual(Neuron::VoxelMask(Neuron::Int3{2, 8, 1}).Hash(), Neuron::VoxelMask(Neuron::Int3{8, 2, 1}).Hash(),
                        L"the size is hashed");
  }

  TEST_METHOD(ASnapshotOrABrickDiffNamedByItsEventCatchesUpAMaskThatMissedEvents)
  {
    const Neuron::VoxModel model = Block(40, 20, 18);
    const Neuron::StrengthTable strengths = Strengths(10);
    Neuron::VoxelMask server = Neuron::VoxelMask::FromModel(model);
    Neuron::VoxelMask late = server; // a client that saw none of the first events
    const Neuron::VoxelMask initial = server;

    for (std::uint64_t event = 1; event <= 6; ++event)
    {
      Neuron::CarveSpec spec = Shot(static_cast<int>(event * 6), 0, 9, 3, 400, static_cast<std::uint32_t>(event));
      spec.sequence = event;
      static_cast<void>(Neuron::Carve(server, model, strengths, spec));
    }
    Assert::AreEqual(std::uint64_t{6}, server.LastEvent());

    // From a snapshot: the late mask takes it, then follows the events after it.
    const Neuron::MaskSnapshot snapshot = Neuron::TakeSnapshot(server);
    Assert::AreEqual(std::uint64_t{6}, snapshot.lastEvent, L"the snapshot names the last event it includes");
    Neuron::VoxelMask fromSnapshot;
    Assert::IsTrue(Neuron::RestoreSnapshot(snapshot, fromSnapshot));
    // From a diff of the bricks that changed since the late mask's state.
    const Neuron::MaskDiff diff = Neuron::DiffBricks(late, server);
    Assert::AreEqual(std::uint64_t{6}, diff.lastEvent);
    Assert::IsTrue(!diff.bricks.empty() && diff.bricks.size() < server.BrickCount(), L"only the bricks that changed");
    Assert::IsTrue(Neuron::ApplyDiff(diff, late));

    for (std::uint64_t event = 7; event <= 9; ++event)
    {
      Neuron::CarveSpec spec = Shot(20, 19, static_cast<int>(event), 2, 300, static_cast<std::uint32_t>(event));
      spec.sequence = event;
      static_cast<void>(Neuron::Carve(server, model, strengths, spec));
      static_cast<void>(Neuron::Carve(fromSnapshot, model, strengths, spec));
      static_cast<void>(Neuron::Carve(late, model, strengths, spec));
    }
    Assert::AreEqual(server.Hash(), fromSnapshot.Hash(), L"snapshot, then the later events");
    Assert::AreEqual(server.Hash(), late.Hash(), L"brick diff, then the later events");
    Assert::AreNotEqual(server.Hash(), initial.Hash());
    Assert::AreEqual(std::uint64_t{9}, late.LastEvent());
  }

  TEST_METHOD(AMalformedSnapshotOrDiffIsRefused)
  {
    Neuron::VoxelMask mask = Neuron::VoxelMask::FromModel(Block(3, 3, 3));
    Neuron::MaskSnapshot snapshot = Neuron::TakeSnapshot(mask);
    snapshot.words.push_back(0);
    Neuron::VoxelMask out;
    Assert::IsFalse(Neuron::RestoreSnapshot(snapshot, out), L"more words than the size needs");
    snapshot = Neuron::TakeSnapshot(mask);
    snapshot.words.back() |= std::uint64_t{1} << 40; // past the 27th cell
    Assert::IsFalse(Neuron::RestoreSnapshot(snapshot, out), L"a bit beyond the last cell");
    Neuron::MaskDiff diff;
    diff.bricks.push_back(Neuron::BrickPatch{5, {}});
    Assert::IsFalse(Neuron::ApplyDiff(diff, mask), L"a brick outside the mask");
    Assert::AreEqual(27u, mask.PresentCount(), L"and nothing changed");
  }

  TEST_METHOD(AbsentSpansRoundTripAMaskAgainstItsArchetype)
  {
    const Neuron::VoxelMask archetype = Neuron::VoxelMask::FromModel(Block(10, 10, 10));
    Neuron::VoxelMask damaged = archetype;
    for (const std::uint32_t index : {3u, 4u, 5u, 99u, 500u, 501u})
    {
      damaged.SetAt(index, false);
    }
    const std::vector<Neuron::CellSpan> spans = Neuron::AbsentSpans(damaged, archetype);
    Assert::AreEqual(std::size_t{3}, spans.size());
    Assert::IsTrue(spans[0] == Neuron::CellSpan{3, 3});
    Assert::IsTrue(spans[2] == Neuron::CellSpan{500, 2});
    Neuron::VoxelMask restored;
    Assert::IsTrue(Neuron::MaskFromAbsentSpans(archetype, spans, restored));
    Assert::AreEqual(damaged.Hash(), restored.Hash());
    const std::vector<Neuron::CellSpan> outside{Neuron::CellSpan{999, 2}};
    Assert::IsFalse(Neuron::MaskFromAbsentSpans(archetype, outside, restored), L"a span past the last cell");
  }
};

TEST_CLASS(VoxelCarveTests)
{
public:
  TEST_METHOD(TheSameEventOnTwoMasksGivesEqualHashes)
  {
    const Neuron::VoxModel model = Block(30, 30, 30);
    Neuron::VoxelMask a = Neuron::VoxelMask::FromModel(model);
    Neuron::VoxelMask b = Neuron::VoxelMask::FromModel(model);
    const Neuron::StrengthTable strengths = Strengths(12);
    for (std::uint32_t seed = 1; seed <= 20; ++seed)
    {
      const Neuron::CarveSpec spec = Shot(static_cast<int>(seed % 30), 29, static_cast<int>((seed * 7) % 30), 4, 900, seed);
      const Neuron::CarveResult first = Neuron::Carve(a, model, strengths, spec);
      const Neuron::CarveResult second = Neuron::Carve(b, model, strengths, spec);
      Assert::IsTrue(first.removed == second.removed, L"the same cells, in the same order");
      Assert::AreEqual(a.Hash(), b.Hash());
    }
  }

  TEST_METHOD(TheSeedRoughensTheEdgeAndNothingElseVaries)
  {
    const Neuron::VoxModel model = Block(20, 20, 20);
    const Neuron::StrengthTable strengths = Strengths(10);
    Neuron::VoxelMask a = Neuron::VoxelMask::FromModel(model);
    Neuron::VoxelMask b = Neuron::VoxelMask::FromModel(model);
    static_cast<void>(Neuron::Carve(a, model, strengths, Shot(10, 19, 10, 4, 300, 1)));
    static_cast<void>(Neuron::Carve(b, model, strengths, Shot(10, 19, 10, 4, 300, 2)));
    Assert::AreNotEqual(a.Hash(), b.Hash(), L"another seed, another edge");
  }

  TEST_METHOD(EnergyIsSpentCellByCellAndNeverOverspent)
  {
    const Neuron::VoxModel model = Block(20, 20, 20);
    const Neuron::StrengthTable strengths = Strengths(10);
    for (const std::uint32_t energy : {0u, 9u, 10u, 15u, 100u, 1000u})
    {
      Neuron::VoxelMask mask = Neuron::VoxelMask::FromModel(model);
      const Neuron::CarveResult result = Neuron::Carve(mask, model, strengths, Shot(10, 19, 10, 3, energy));
      Assert::IsTrue(result.energySpent <= energy);
      // Each cell costs 10 to 15: the count is bounded by the energy on both sides.
      Assert::IsTrue(result.removed.size() * 10 <= energy);
      Assert::IsTrue(energy < 15 || !result.removed.empty(), L"enough for one cell removes one");
      Assert::AreEqual(8000u - static_cast<std::uint32_t>(result.removed.size()), mask.PresentCount());
    }
    Neuron::VoxelMask mask = Neuron::VoxelMask::FromModel(model);
    Assert::IsTrue(Neuron::Carve(mask, model, strengths, Shot(10, 19, 10, 3, 9)).removed.empty(),
                   L"less than the cheapest cell removes nothing");
  }

  TEST_METHOD(ASphereStaysInsideItsRadiusAndACapsuleReachesAlongTheShot)
  {
    const Neuron::VoxModel model = Block(40, 40, 40);
    const Neuron::StrengthTable strengths = Strengths(1);
    Neuron::VoxelMask sphere = Neuron::VoxelMask::FromModel(model);
    const Neuron::CarveResult round = Neuron::Carve(sphere, model, strengths, Shot(20, 39, 20, 3, 100000));
    for (const std::uint32_t index : round.removed)
    {
      const Neuron::Int3 cell = sphere.CellOf(index);
      const int dx = cell.x - 20;
      const int dy = cell.y - 39;
      const int dz = cell.z - 20;
      Assert::IsTrue(dx * dx + dy * dy + dz * dz <= 9, L"every removed cell lies within the radius");
    }
    Assert::IsFalse(sphere.Present(20, 37, 20), L"the half-sphere under the impact is gone");
    Assert::IsTrue(sphere.Present(20, 35, 20));

    Neuron::VoxelMask capsule = Neuron::VoxelMask::FromModel(model);
    Neuron::CarveSpec spec = Shot(20, 39, 20, 1, 100000);
    spec.shape = Neuron::CarveShape::Capsule;
    spec.direction = Neuron::Int3{0, -64, 0};
    spec.lengthCells = 12;
    static_cast<void>(Neuron::Carve(capsule, model, strengths, spec));
    Assert::IsFalse(capsule.Present(20, 28, 20), L"the capsule bores along the shot");
    Assert::IsTrue(capsule.Present(20, 26, 20), L"and stops after its length");
    Assert::IsTrue(capsule.Present(23, 35, 20), L"no wider than its radius");
  }

  TEST_METHOD(AnUnbrokenLayerStopsTheCarveBehindIt)
  {
    // A plate of unbreakable colour 2 at y = 18, over ordinary cells.
    Neuron::VoxModel model = Block(20, 20, 20);
    for (int z = 0; z < 20; ++z)
    {
      for (int x = 0; x < 20; ++x)
      {
        model.Set(x, 18, z, 2);
      }
    }
    Neuron::VoxelMask mask = Neuron::VoxelMask::FromModel(model);
    static_cast<void>(Neuron::Carve(mask, model, Strengths(1), Shot(10, 19, 10, 4, 100000)));
    Assert::IsFalse(mask.Present(10, 19, 10), L"the skin above the plate goes");
    Assert::IsTrue(mask.Present(10, 18, 10), L"the plate holds");
    Assert::IsTrue(mask.Present(10, 17, 10), L"and nothing behind it was reached");
  }

  TEST_METHOD(ACellOrderThatDoesNotDependOnContainers)
  {
    // The order is the documented key, whatever the inputs' history: carving one mask in pieces and
    // another whole with the same events gives the same cells.
    const Neuron::VoxModel model = Block(16, 16, 16);
    Neuron::VoxelMask a = Neuron::VoxelMask::FromModel(model);
    const Neuron::CarveResult result = Neuron::Carve(a, model, Strengths(5), Shot(8, 15, 8, 2, 120, 3));
    std::vector<std::uint32_t> sorted = result.removed;
    std::ranges::sort(sorted);
    Assert::IsTrue(result.removed.front() == a.Index(8, 15, 8), L"the impact cell goes first");
    Assert::IsTrue(result.removed != sorted || result.removed.size() < 3, L"in distance order, not index order");
  }

  TEST_METHOD(APatchRestoresOnlyAllowedCellsThatTouchTheStructure)
  {
    const Neuron::VoxModel model = Block(10, 10, 10);
    const Neuron::VoxelMask archetype = Neuron::VoxelMask::FromModel(model);
    Neuron::VoxelMask mask = archetype;
    static_cast<void>(Neuron::Carve(mask, model, Strengths(1), Shot(5, 9, 5, 3, 100000)));
    const std::uint32_t removed = archetype.PresentCount() - mask.PresentCount();
    // Only cells with x below 5 may be restored.
    Neuron::VoxelMask allowed(archetype.Size());
    for (std::uint32_t i = 0; i < archetype.CellCount(); ++i)
    {
      allowed.SetAt(i, archetype.CellOf(i).x < 5);
    }
    const std::vector<std::uint32_t> restored = Neuron::Patch(mask, allowed, Neuron::PatchSpec{2, 1000});
    Assert::IsTrue(!restored.empty() && restored.size() < removed);
    for (const std::uint32_t index : restored)
    {
      Assert::IsTrue(mask.CellOf(index).x < 5);
    }
    Assert::AreEqual(std::uint64_t{2}, mask.LastEvent());
    Neuron::VoxelMask capped = archetype;
    static_cast<void>(Neuron::Carve(capped, model, Strengths(1), Shot(5, 9, 5, 3, 100000)));
    Assert::AreEqual(std::size_t{4}, Neuron::Patch(capped, archetype, Neuron::PatchSpec{2, 4}).size(), L"no more than asked");
  }

  TEST_METHOD(PartsCutFromTheAnchorAreFoundWhole)
  {
    // A bar 10 long; cutting cell 4 leaves 0..3 on the anchor and 5..9 adrift.
    Neuron::VoxelMask mask = Neuron::VoxelMask::FromModel(Block(10, 1, 1));
    mask.SetAt(4, false);
    const std::vector<std::uint32_t> anchors{0};
    const std::vector<std::uint32_t> removed{4};
    std::vector<std::vector<std::uint32_t>> parts = Neuron::DetachedParts(mask, anchors, removed);
    Assert::AreEqual(std::size_t{1}, parts.size());
    Assert::IsTrue(parts[0] == std::vector<std::uint32_t>{5, 6, 7, 8, 9});
    // Cut again: two parts, in order of their first cell.
    mask.SetAt(7, false);
    parts = Neuron::DetachedParts(mask, anchors, removed);
    Assert::AreEqual(std::size_t{2}, parts.size());
    Assert::AreEqual(5u, parts[0].front());
    Assert::AreEqual(8u, parts[1].front());
    // No anchor left: everything present is adrift.
    mask.SetAt(0, false);
    parts = Neuron::DetachedParts(mask, anchors, removed);
    Assert::AreEqual(std::size_t{3}, parts.size());
  }

  TEST_METHOD(ACutThroughTheFrigatesMidsectionSeparatesTheBow)
  {
    // ADR-021 measured it: a three-cell-wide cut across the whole grid at VOX Y = 70..72 detaches
    // 38,109 cells from the frigate's core.
    const Neuron::VoxModel model = LoadModel("FrigateIntact.vox");
    Neuron::VoxelMask mask = Neuron::VoxelMask::FromModel(model);
    std::vector<std::uint32_t> removed;
    for (int z = 0; z < mask.Size().z; ++z)
    {
      for (int y = 70; y <= 72; ++y)
      {
        for (int x = 0; x < mask.Size().x; ++x)
        {
          if (mask.Present(x, y, z))
          {
            removed.push_back(mask.Index(x, y, z));
            mask.SetAt(removed.back(), false);
          }
        }
      }
    }
    // The core region of Assets/Hulls/Hulls.txt: min 22,28,12, max 26,34,16.
    std::vector<std::uint32_t> core;
    for (int z = 12; z < 16; ++z)
    {
      for (int y = 28; y < 34; ++y)
      {
        for (int x = 22; x < 26; ++x)
        {
          core.push_back(mask.Index(x, y, z));
        }
      }
    }
    const std::vector<std::vector<std::uint32_t>> parts = Neuron::DetachedParts(mask, core, removed);
    const std::size_t detached =
      std::accumulate(parts.begin(), parts.end(), std::size_t{0}, [](std::size_t _sum, const auto& _part) { return _sum + _part.size(); });
    Assert::AreEqual(std::size_t{38109}, detached);
    for (const auto& part : parts)
    {
      Assert::IsTrue(mask.CellOf(part.front()).y > 72 || part.size() < 100, L"what goes is the bow, beyond the cut");
    }
  }
};

} // namespace NeuronCoreTests
