#include "SceneAssets.h"

#include "Palette.h"
#include "VoxFile.h"

#include <array>
#include <cmath>

namespace Outpost
{

namespace
{

[[nodiscard]] float SrgbToLinear(std::uint8_t _value) noexcept
{
  const float c = static_cast<float>(_value) / 255.0f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

/// The renderer's material table: the canonical palette as linear albedo, plus the emission the
/// game assigns to its emissive indices (ADR-002). This is the only place they meet.
[[nodiscard]] std::array<Neuron::GpuMaterial, Neuron::Renderer::MATERIAL_COUNT> BuildGpuMaterials()
{
  const Neuron::VoxPalette& palette = CanonicalPalette();
  const Neuron::VoxelMaterialTable& materials = MaterialTable();
  std::array<Neuron::GpuMaterial, Neuron::Renderer::MATERIAL_COUNT> table{};
  for (std::size_t i = 0; i < table.size(); ++i)
  {
    table[i].albedo = DirectX::XMFLOAT4{SrgbToLinear(palette[i].r), SrgbToLinear(palette[i].g), SrgbToLinear(palette[i].b), 1.0f};
    table[i].emission = DirectX::XMFLOAT4{materials[i].emissionR, materials[i].emissionG, materials[i].emissionB, materials[i].glowRadius};
  }
  return table;
}

} // namespace

bool SceneAssets::Load(const std::filesystem::path& _assetRoot, Neuron::Renderer& _renderer, std::string& _outError)
{
  m_models.clear();
  m_warnings.clear();
  if (!LoadScene(_assetRoot / "Scene.txt", m_scene, _outError))
  {
    return false;
  }
  const std::vector<std::string> problems = ValidateScene(m_scene, _assetRoot);
  if (!problems.empty())
  {
    _outError = problems.front();
    for (std::size_t i = 1; i < problems.size() && i < 8; ++i)
    {
      _outError += "\n" + problems[i];
    }
    return false;
  }

  const auto gpuMaterials = BuildGpuMaterials();
  if (!_renderer.SetMaterials(gpuMaterials))
  {
    _outError = "material table upload failed";
    return false;
  }

  const Neuron::VoxPalette& canonical = CanonicalPalette();
  const Neuron::VoxelMaterialTable& materials = MaterialTable();
  for (ModelAsset& asset : m_scene.models)
  {
    Neuron::VoxDocument document;
    const Neuron::VoxError error = Neuron::LoadVoxFile(_assetRoot / asset.file, document);
    if (error != Neuron::VoxError::None)
    {
      _outError = asset.file + ": " + std::string(Neuron::VoxErrorName(error));
      return false;
    }
    if (!document.hasPalette || document.palette != canonical)
    {
      m_warnings.push_back(asset.file + ": palette differs from the canonical palette; colours come from the canonical table");
    }
    asset.SetSize(Neuron::Int3{static_cast<int>(document.model.SizeX()), static_cast<int>(document.model.SizeY()),
                               static_cast<int>(document.model.SizeZ())});

    LoadedModel model;
    model.name = asset.name;
    model.surface = Neuron::ExtractSurface(document.model);
    model.gpu = _renderer.UploadModel(model.surface);
    if (model.gpu.voxelCount != model.surface.voxels.size())
    {
      _outError = asset.file + ": GPU upload failed";
      return false;
    }
    model.blueprintEdges =
      Neuron::BuildCoarseWireframe(document.model, BLUEPRINT_BLOCK_VOXELS, BLUEPRINT_FILL_PERCENT, BLUEPRINT_GRID_BLOCKS);
    model.sideProfile = Neuron::BuildSideProfile(model.surface, PROFILE_DETAIL_DEPTH_VOXELS);
    for (const Neuron::SurfaceVoxel& voxel : model.surface.voxels)
    {
      const Neuron::VoxelMaterial& material = materials[voxel.colorIndex];
      if (material.glowRadius > 0.0f)
      {
        model.glows.push_back(GlowPoint{
          DirectX::XMFLOAT3{static_cast<float>(voxel.x) + 0.5f, static_cast<float>(voxel.y) + 0.5f, static_cast<float>(voxel.z) + 0.5f},
          DirectX::XMFLOAT3{material.emissionR, material.emissionG, material.emissionB}, material.glowRadius});
      }
    }
    m_models.push_back(std::move(model));
  }
  return true;
}

const LoadedModel* SceneAssets::FindModel(std::string_view _name) const noexcept
{
  for (const LoadedModel& model : m_models)
  {
    if (model.name == _name)
    {
      return &model;
    }
  }
  return nullptr;
}

std::size_t SceneAssets::SurfaceVoxelCount() const noexcept
{
  std::size_t count = 0;
  for (const LoadedModel& model : m_models)
  {
    count += model.surface.voxels.size();
  }
  return count;
}

} // namespace Outpost
