#pragma once

#include "GpuTypes.h"
#include "GraphicsDevice.h"
#include "VoxelMesher.h"

#include <cstdint>
#include <span>
#include <string>

namespace Neuron
{

/// A model's surface voxels on the GPU, one GpuVoxel per voxel with an exposed face.
struct GpuVoxelModel
{
  ComPtr<ID3D12Resource> voxels;
  std::uint32_t voxelCount = 0;
};

/// The passes of a frame and the state they share (ADR-003): one root signature for every
/// pipeline, the voxel pipelines and their shadow map, and the additive line, effect and HUD
/// pipelines. The renderer knows voxels, lines, light effects and HUD triangles — never a ship,
/// a station or a palette role; the game fills the material table and decides what to draw.
class Renderer
{
public:
  static constexpr std::uint32_t SHADOW_MAP_SIZE = 4096;
  static constexpr std::uint32_t MATERIAL_COUNT = 256;

  [[nodiscard]] bool Initialize(GraphicsDevice& _device, std::string& _outError);

  /// The per-palette-index material table every voxel draw reads.
  [[nodiscard]] bool SetMaterials(std::span<const GpuMaterial, MATERIAL_COUNT> _materials);

  /// Replaces the HUD atlas (R8 coverage, white texel block for solid fills).
  [[nodiscard]] bool SetHudAtlas(std::uint32_t _width, std::uint32_t _height, std::span<const std::uint8_t> _coverage);

  [[nodiscard]] GpuVoxelModel UploadModel(const VoxelSurface& _surface);

  /// Binds the shared root signature and descriptor heap, pushes the frame constants, and sets
  /// the viewport and scissor from them. Every pass begins here.
  void BeginPass(const FrameConstants& _frame);

  /// Clears the depth buffer inside the current pass's viewport only.
  void ClearDepthInViewport();

  void BeginShadowPass(const FrameConstants& _frame);
  void EndShadowPass();
  void DrawVoxelsShadow(const GpuVoxelModel& _model, const VoxelDrawConstants& _constants);

  void DrawVoxels(const GpuVoxelModel& _model, const VoxelDrawConstants& _constants);
  void DrawLines(std::span<const GpuLineSegment> _segments);
  void DrawEffects(std::span<const GpuEffect> _effects);
  void DrawHud(std::span<const GpuHudVertex> _vertices);

  /// Restores the back buffer and main depth buffer as the render targets.
  void BindMainTargets();

  /// Clears the whole back buffer to a linear colour and the main depth buffer to the far plane.
  void ClearMainTargets(const float (&_linearColor)[4]);

private:
  [[nodiscard]] bool CreateRootSignature();
  [[nodiscard]] bool CreatePipelines();
  [[nodiscard]] bool CreateShadowMap();
  [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS PushArray(const void* _data, std::uint64_t _bytes);

  GraphicsDevice* m_device = nullptr;
  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12PipelineState> m_voxelPipeline;
  ComPtr<ID3D12PipelineState> m_voxelShadowPipeline;
  ComPtr<ID3D12PipelineState> m_linePipeline;
  ComPtr<ID3D12PipelineState> m_effectPipeline;
  ComPtr<ID3D12PipelineState> m_hudPipeline;
  ComPtr<ID3D12DescriptorHeap> m_shaderHeap; // [0] shadow map, [1] HUD atlas
  ComPtr<ID3D12DescriptorHeap> m_shadowDsvHeap;
  ComPtr<ID3D12Resource> m_shadowMap;
  ComPtr<ID3D12Resource> m_materials;
  ComPtr<ID3D12Resource> m_hudAtlas;
  std::uint32_t m_shaderDescriptorSize = 0;
  D3D12_GPU_VIRTUAL_ADDRESS m_frameConstants = 0;
  D3D12_VIEWPORT m_viewport{};
  D3D12_RECT m_scissor{};
};

} // namespace Neuron
