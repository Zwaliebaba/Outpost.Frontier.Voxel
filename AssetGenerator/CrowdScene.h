#pragma once

#include "VoxModel.h"

#include <filesystem>

namespace AssetGen
{

// The crowd benchmark of plan task F6: a generated scene at Design/GameDesign.md §8.2's load —
// 600 ships within a kilometre of each other and 20 outposts with their claim asteroids around
// them — written in four variants, each a complete asset root with its own Scene.txt:
//
//   Battle       the camera inside the crowd, every object at full detail
//   BattleLod    the same, with distant objects swapped for 2x and 4x downsampled copies
//   Overview     the camera outside, the whole crowd in view, full detail
//   OverviewLod  the same, with the stand-ins
//
// The stand-ins are a crude substitute for levels of detail, there only to measure whether
// ADR-003's renderer could reach the budget with them. Nothing here is shipped content.

/// A copy at 1/_factor resolution: a coarse cell is solid when at least a quarter of its fine
/// cells are, and takes their most frequent colour (the lowest index on a tie).
[[nodiscard]] Neuron::VoxModel Downsample(const Neuron::VoxModel& _model, int _factor);

/// Writes the four variants under _outDir. Returns the number of files that could not be written.
[[nodiscard]] int WriteCrowdScenes(const std::filesystem::path& _outDir);

} // namespace AssetGen
