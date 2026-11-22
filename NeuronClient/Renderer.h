#pragma once

#include "DdsFile.h"
#include "GpuTypes.h"
#include "GraphicsDevice.h"
#include "VoxelMesher.h"

#include <array>
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
  /// HUD texture slots (ADR-023): slot 0 is the font atlas, the others hold sprite atlases. A HUD
  /// vertex names its slot, so text, solid fills and sprites share one draw per layer.
  static constexpr std::uint32_t HUD_TEXTURE_COUNT = 4;
  static constexpr std::uint32_t HUD_FONT_SLOT = 0;

  [[nodiscard]] bool Initialize(GraphicsDevice& _device, std::string& _outError);

  /// The per-palette-index material table every voxel draw reads.
  [[nodiscard]] bool SetMaterials(std::span<const GpuMaterial, MATERIAL_COUNT> _materials);

  /// Replaces the HUD font atlas in slot 0 (R8 coverage, white texel block for solid fills).
  [[nodiscard]] bool SetHudAtlas(std::uint32_t _width, std::uint32_t _height, std::span<const std::uint8_t> _coverage);

  /// Uploads a static sprite atlas into HUD texture slot 1 to HUD_TEXTURE_COUNT - 1, after checking
  /// that the device samples its format as a 2D texture. Block-compressed rows are laid out by the
  /// device's copyable footprint. On failure the slot keeps what it held and _outError says why.
  [[nodiscard]] bool SetHudTexture(std::uint32_t _slot, const DdsImage& _image, std::string& _outError);

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
  void CreateHudView(std::uint32_t _slot, ID3D12Resource* _texture, DXGI_FORMAT _format);

  GraphicsDevice* m_device = nullptr;
  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12PipelineState> m_voxelPipeline;
  ComPtr<ID3D12PipelineState> m_voxelShadowPipeline;
  ComPtr<ID3D12PipelineState> m_linePipeline;
  ComPtr<ID3D12PipelineState> m_effectPipeline;
  ComPtr<ID3D12PipelineState> m_hudPipeline;
  ComPtr<ID3D12DescriptorHeap> m_shaderHeap; // [0] shadow map, [1 + slot] HUD textures
  ComPtr<ID3D12DescriptorHeap> m_shadowDsvHeap;
  ComPtr<ID3D12Resource> m_shadowMap;
  ComPtr<ID3D12Resource> m_materials;
  std::array<ComPtr<ID3D12Resource>, HUD_TEXTURE_COUNT> m_hudTextures;
  std::uint32_t m_shaderDescriptorSize = 0;
  D3D12_GPU_VIRTUAL_ADDRESS m_frameConstants = 0;
  D3D12_VIEWPORT m_viewport{};
  D3D12_RECT m_scissor{};
};

} // namespace Neuron
