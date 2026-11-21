#pragma once

#include "VoxModel.h"
#include "VoxelBuilder.h"

#include <cstdint>
#include <span>

namespace AssetGen
{

/// One generated asset: the file stem under Assets/Models/ and the function that builds it.
struct ModelRecipe
{
  const char* name;
  Neuron::VoxModel (*build)();
};

/// Every model the generator writes, in a fixed order.
[[nodiscard]] std::span<const ModelRecipe> Recipes() noexcept;

// ── Shared painting ──────────────────────────────────────────────────────────────────────────

/// The colour shape builders lay down before painting. PaintHull replaces only this index, so
/// lamps, glass, steel and decking placed by hand survive the paint pass.
inline constexpr std::uint8_t PLACEHOLDER = 1;

/// Weights, in percent, of the four plating families a hull panel can be.
struct PaintMix
{
  int charcoal;
  int gray;
  int white;
  int orange;
};

/// Paints PLACEHOLDER voxels in a box with blocky plates: each panel cell picks a family by
/// weight and a shade within it, both from a positional hash.
void PaintHull(VoxelBuilder& _builder, Int3 _min, Int3 _max, PaintMix _mix, Int3 _panel, std::uint32_t _salt) noexcept;

/// Paints the outermost solid voxel along +X (or -X) at each (y, z) of a hexagonal emblem.
void PaintHexOnSide(VoxelBuilder& _builder, bool _positiveX, int _cy, int _cz, int _radius, std::uint8_t _plate,
                    std::uint8_t _mark) noexcept;

// ── Builders ─────────────────────────────────────────────────────────────────────────────────
[[nodiscard]] Neuron::VoxModel BuildFrigate();
[[nodiscard]] Neuron::VoxModel BuildEscortLancer();
[[nodiscard]] Neuron::VoxModel BuildEscortTalon();
[[nodiscard]] Neuron::VoxModel BuildTug();
[[nodiscard]] Neuron::VoxModel BuildCargoModule();
[[nodiscard]] Neuron::VoxModel BuildHangarBay();
[[nodiscard]] Neuron::VoxModel BuildHangarSection();
[[nodiscard]] Neuron::VoxModel BuildCommandBlock();
[[nodiscard]] Neuron::VoxModel BuildTankCluster();
[[nodiscard]] Neuron::VoxModel BuildDockingCollar();
[[nodiscard]] Neuron::VoxModel BuildTrussBeam();
[[nodiscard]] Neuron::VoxModel BuildTrussTower();
[[nodiscard]] Neuron::VoxModel BuildAsteroidMajor();
[[nodiscard]] Neuron::VoxModel BuildAsteroidSmallA();
[[nodiscard]] Neuron::VoxModel BuildAsteroidSmallB();
[[nodiscard]] Neuron::VoxModel BuildAsteroidSmallC();
[[nodiscard]] Neuron::VoxModel BuildDebrisA();
[[nodiscard]] Neuron::VoxModel BuildDebrisB();
[[nodiscard]] Neuron::VoxModel BuildDebrisC();

} // namespace AssetGen
