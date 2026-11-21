#pragma once

#include "VoxelMesher.h"

#include <vector>

namespace Neuron
{

/// A wireframe segment between two lattice points, in VOX coordinates.
struct LatticeSegment
{
  Int3 a;
  Int3 b;
};

/// The wireframe of a voxel surface, built from the same faces the solid model has: every crease
/// (an edge where the surface turns) plus, on flat runs, the lattice lines every _gridStep voxels.
/// Collinear unit edges are merged, and the output order is deterministic. Pass _gridStep 0 for
/// creases only.
[[nodiscard]] std::vector<LatticeSegment> BuildVoxelWireframe(const VoxelSurface& _surface, int _gridStep);

/// The wireframe of a coarse copy of the model, for a blueprint that should read as the module's
/// form rather than every greeble on it. Each _blockVoxels-sided block of the model is solid when
/// at least _fillPercent of its in-bounds cells are; the coarse model's wireframe (see above, with
/// _gridStep counted in blocks) is scaled back up and clamped to the model's bounds, so segments
/// are in the original model's VOX coordinates.
[[nodiscard]] std::vector<LatticeSegment> BuildCoarseWireframe(const VoxModel& _model, int _blockVoxels, int _fillPercent, int _gridStep);

/// A side elevation seen from +X: the model projected onto the VOX y-z plane.
struct SideProfile
{
  std::vector<LatticeSegment> outline; // between covered and uncovered cells of the projection
  std::vector<LatticeSegment> detail;  // inside the projection, where the nearest surface steps by
                                       // at least the requested depth: panel and deck edges
};

/// Segments lie in the plane x = the surface's centre, in VOX coordinates.
[[nodiscard]] SideProfile BuildSideProfile(const VoxelSurface& _surface, int _detailDepthVoxels);

} // namespace Neuron
