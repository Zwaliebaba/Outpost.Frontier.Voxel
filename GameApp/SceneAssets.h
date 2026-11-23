#pragma once

#include "Renderer.h"
#include "SceneDefinition.h"
#include "VoxelMesher.h"
#include "VoxelWireframe.h"

#include <DirectXMath.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Outpost
{

/// An emissive voxel that gets a glow sprite (its material has a glow radius).
struct GlowPoint
{
  DirectX::XMFLOAT3 centerVox;
  DirectX::XMFLOAT3 color;
  float radiusVoxels;
  float strobePeriodSeconds;
};

/// One model, loaded once and shared by every instance that uses it.
struct LoadedModel
{
  std::string name;
  Neuron::VoxModel voxels; // as loaded: a damaged instance's appearance is taken from it (ADR-013)
  Neuron::VoxelSurface surface;
  Neuron::GpuVoxelModel gpu;
  std::vector<Neuron::LatticeSegment> blueprintEdges; // coarse wireframe, for blueprint instances
  Neuron::SideProfile sideProfile;                    // side elevation, for the selected-ship panel
  std::vector<GlowPoint> glows;
};

/// The glow sprites of a surface's emissive voxels.
[[nodiscard]] std::vector<GlowPoint> BuildGlows(const Neuron::VoxelSurface& _surface);

/// Everything the client loads from Assets: the scene definition, every model it names, the GPU
/// buffers and wireframes built from them, and the renderer's material table.
class SceneAssets
{
public:
  // The blueprint wireframe is built from a coarse copy of the module (VoxelWireframe.h): blocks
  // of BLUEPRINT_BLOCK_VOXELS, solid at BLUEPRINT_FILL_PERCENT occupancy, with a lattice line every
  // BLUEPRINT_GRID_BLOCKS blocks on flat runs.
  static constexpr int BLUEPRINT_BLOCK_VOXELS = 4;
  static constexpr int BLUEPRINT_FILL_PERCENT = 30;
  static constexpr int BLUEPRINT_GRID_BLOCKS = 3;
  // A step in the side elevation's nearest surface of at least this many voxels draws a line.
  static constexpr int PROFILE_DETAIL_DEPTH_VOXELS = 3;

  [[nodiscard]] bool Load(const std::filesystem::path& _assetRoot, Neuron::Renderer& _renderer, std::string& _outError);

  [[nodiscard]] const SceneDefinition& Scene() const noexcept
  {
    return m_scene;
  }
  [[nodiscard]] const LoadedModel* FindModel(std::string_view _name) const noexcept;
  [[nodiscard]] std::size_t ModelCount() const noexcept
  {
    return m_models.size();
  }
  [[nodiscard]] std::size_t SurfaceVoxelCount() const noexcept;

  /// Non-fatal findings from loading, such as a model whose palette is not the canonical one.
  [[nodiscard]] const std::vector<std::string>& Warnings() const noexcept
  {
    return m_warnings;
  }

private:
  SceneDefinition m_scene;
  std::vector<LoadedModel> m_models;
  std::vector<std::string> m_warnings;
};

} // namespace Outpost
