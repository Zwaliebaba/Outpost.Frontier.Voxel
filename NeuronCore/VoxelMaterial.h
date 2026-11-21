#pragma once

#include <array>

namespace Neuron
{

/// What a palette index emits, independent of its albedo. The renderer uploads one table of these
/// for every model it draws, so an index means the same thing in every asset (ADR-003). The game
/// decides the values; the engine only knows the layout.
struct VoxelMaterial
{
  float emissionR; // linear radiance added on top of lit albedo
  float emissionG;
  float emissionB;
  float glowRadius; // world-space radius of the additive glow sprite placed on such voxels; 0 = none
};

using VoxelMaterialTable = std::array<VoxelMaterial, 256>;

} // namespace Neuron
