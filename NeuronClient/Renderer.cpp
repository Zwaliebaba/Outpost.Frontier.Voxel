#include "Renderer.h"

#include "CompiledShaders/EffectPS.h"
#include "CompiledShaders/EffectVS.h"
#include "CompiledShaders/HudPS.h"
#include "CompiledShaders/HudVS.h"
#include "CompiledShaders/LinePS.h"
#include "CompiledShaders/LineVS.h"
#include "CompiledShaders/VoxelPS.h"
#include "CompiledShaders/VoxelShadowPS.h"
#include "CompiledShaders/VoxelVS.h"

#include <cstring>
#include <vector>

namespace Neuron
{

namespace
{

enum RootParameter : UINT
{
  RootFrame = 0,
  RootDraw,
  RootBufferA,
  RootBufferB,
  RootTextures,
  RootParameterCount
};

[[nodiscard]] D3D12_SHADER_BYTECODE Bytecode(const unsigned char* _data, std::size_t _size) noexcept
{
  return D3D12_SHADER_BYTECODE{_data, _size};
}

[[nodiscard]] D3D12_BLEND_DESC BlendState(bool _enable, D3D12_BLEND _source, D3D12_BLEND _destination) noexcept
{
  // Every field is spelled out: several D3D12 enums have no zero enumerator, so a value-initialized
  // descriptor holds invalid values until each one is assigned.
  const D3D12_RENDER_TARGET_BLEND_DESC target{.BlendEnable = _enable ? TRUE : FALSE,
                                              .LogicOpEnable = FALSE,
                                              .SrcBlend = _source,
                                              .DestBlend = _destination,
                                              .BlendOp = D3D12_BLEND_OP_ADD,
                                              .SrcBlendAlpha = D3D12_BLEND_ONE,
                                              .DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA,
                                              .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                                              .LogicOp = D3D12_LOGIC_OP_NOOP,
                                              .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL};
  return D3D12_BLEND_DESC{.AlphaToCoverageEnable = FALSE,
                          .IndependentBlendEnable = FALSE,
                          .RenderTarget = {target, target, target, target, target, target, target, target}};
}

[[nodiscard]] D3D12_RASTERIZER_DESC RasterizerState() noexcept
{
  return D3D12_RASTERIZER_DESC{.FillMode = D3D12_FILL_MODE_SOLID,
                               .CullMode = D3D12_CULL_MODE_NONE,
                               .FrontCounterClockwise = FALSE,
                               .DepthBias = 0,
                               .DepthBiasClamp = 0.0f,
                               .SlopeScaledDepthBias = 0.0f,
                               .DepthClipEnable = TRUE,
                               .MultisampleEnable = FALSE,
                               .AntialiasedLineEnable = FALSE,
                               .ForcedSampleCount = 0,
                               .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
}

[[nodiscard]] D3D12_DEPTH_STENCIL_DESC DepthState(bool _test, bool _write, D3D12_COMPARISON_FUNC _function) noexcept
{
  constexpr D3D12_DEPTH_STENCILOP_DESC KEEP{.StencilFailOp = D3D12_STENCIL_OP_KEEP,
                                            .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
                                            .StencilPassOp = D3D12_STENCIL_OP_KEEP,
                                            .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS};
  return D3D12_DEPTH_STENCIL_DESC{.DepthEnable = _test ? TRUE : FALSE,
                                  .DepthWriteMask = _write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO,
                                  .DepthFunc = _function,
                                  .StencilEnable = FALSE,
                                  .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
                                  .StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
                                  .FrontFace = KEEP,
                                  .BackFace = KEEP};
}

} // namespace

bool Renderer::Initialize(GraphicsDevice& _device, std::string& _outError)
{
  m_device = &_device;
  if (!CreateRootSignature())
  {
    _outError = "root signature creation failed";
    return false;
  }
  if (!CreatePipelines())
  {
    _outError = "pipeline state creation failed";
    return false;
  }
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = 1 + HUD_TEXTURE_COUNT;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(_device.Device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_shaderHeap))))
  {
    _outError = "shader descriptor heap creation failed";
    return false;
  }
  m_shaderDescriptorSize = _device.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (!CreateShadowMap())
  {
    _outError = "shadow map creation failed";
    return false;
  }
  // Sprite slots start as null views, which sample as zero, until the game supplies its atlases.
  for (std::uint32_t slot = 0; slot < HUD_TEXTURE_COUNT; ++slot)
  {
    CreateHudView(slot, nullptr, DXGI_FORMAT_R8G8B8A8_UNORM);
  }
  // A white atlas until the HUD supplies its own, so solid fills draw from the first frame.
  const std::uint8_t white[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
  if (!SetHudAtlas(4, 4, white))
  {
    _outError = "HUD atlas creation failed";
    return false;
  }
  return true;
}

bool Renderer::CreateRootSignature()
{
  D3D12_DESCRIPTOR_RANGE1 textureRange{};
  textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  textureRange.NumDescriptors = 1 + HUD_TEXTURE_COUNT;
  textureRange.BaseShaderRegister = 2;
  textureRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
  textureRange.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER1 parameters[RootParameterCount]{};
  parameters[RootFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[RootFrame].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
  parameters[RootDraw].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[RootDraw].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
  parameters[RootBufferA].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[RootBufferA].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
  parameters[RootBufferB].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[RootBufferB].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
  parameters[RootTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[RootTextures].DescriptorTable = {1, &textureRange};
  parameters[RootTextures].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  for (UINT i = 0; i < RootTextures; ++i)
  {
    parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  // s0: the shadow map's comparison sampler. s1: linear clamp for the HUD textures.
  const D3D12_STATIC_SAMPLER_DESC samplers[2] = {{.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
                                                  .AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                                  .AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                                  .AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                                                  .MipLODBias = 0.0f,
                                                  .MaxAnisotropy = 0,
                                                  .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                                                  .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
                                                  .MinLOD = 0.0f,
                                                  .MaxLOD = D3D12_FLOAT32_MAX,
                                                  .ShaderRegister = 0,
                                                  .RegisterSpace = 0,
                                                  .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL},
                                                 {.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                                                  .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                                  .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                                  .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                                  .MipLODBias = 0.0f,
                                                  .MaxAnisotropy = 0,
                                                  .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
                                                  .BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
                                                  .MinLOD = 0.0f,
                                                  .MaxLOD = D3D12_FLOAT32_MAX,
                                                  .ShaderRegister = 1,
                                                  .RegisterSpace = 0,
                                                  .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL}};

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{D3D_ROOT_SIGNATURE_VERSION_1_1, {}};
  desc.Desc_1_1 = D3D12_ROOT_SIGNATURE_DESC1{.NumParameters = RootParameterCount,
                                             .pParameters = parameters,
                                             .NumStaticSamplers = 2,
                                             .pStaticSamplers = samplers,
                                             .Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                                      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                                      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3D12SerializeVersionedRootSignature(&desc, &blob, &errors)))
  {
    if (errors)
    {
      OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    }
    return false;
  }
  return SUCCEEDED(
    m_device->Device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
}

bool Renderer::CreatePipelines()
{
  ID3D12Device* device = m_device->Device();
  // The shaders are filled in per pipeline below; every other field is stated here.
  const D3D12_GRAPHICS_PIPELINE_STATE_DESC base{.pRootSignature = m_rootSignature.Get(),
                                                .VS = {},
                                                .PS = {},
                                                .DS = {},
                                                .HS = {},
                                                .GS = {},
                                                .StreamOutput = {},
                                                .BlendState = BlendState(false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO),
                                                .SampleMask = UINT_MAX,
                                                .RasterizerState = RasterizerState(),
                                                .DepthStencilState = DepthState(false, false, D3D12_COMPARISON_FUNC_ALWAYS),
                                                .InputLayout = {},
                                                .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
                                                .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                                                .NumRenderTargets = 1,
                                                .RTVFormats = {GraphicsDevice::RENDER_TARGET_FORMAT},
                                                .DSVFormat = GraphicsDevice::DEPTH_FORMAT,
                                                .SampleDesc = {.Count = 1, .Quality = 0},
                                                .NodeMask = 0,
                                                .CachedPSO = {},
                                                .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};

  D3D12_GRAPHICS_PIPELINE_STATE_DESC voxel = base;
  voxel.VS = Bytecode(g_VoxelVS, sizeof(g_VoxelVS));
  voxel.PS = Bytecode(g_VoxelPS, sizeof(g_VoxelPS));
  voxel.BlendState = BlendState(false, D3D12_BLEND_ONE, D3D12_BLEND_ZERO);
  voxel.DepthStencilState = DepthState(true, true, D3D12_COMPARISON_FUNC_LESS);
  if (FAILED(device->CreateGraphicsPipelineState(&voxel, IID_PPV_ARGS(&m_voxelPipeline))))
  {
    return false;
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC shadow = voxel;
  shadow.PS = Bytecode(g_VoxelShadowPS, sizeof(g_VoxelShadowPS));
  shadow.NumRenderTargets = 0;
  shadow.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
  if (FAILED(device->CreateGraphicsPipelineState(&shadow, IID_PPV_ARGS(&m_voxelShadowPipeline))))
  {
    return false;
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC line = base;
  line.VS = Bytecode(g_LineVS, sizeof(g_LineVS));
  line.PS = Bytecode(g_LinePS, sizeof(g_LinePS));
  line.BlendState = BlendState(true, D3D12_BLEND_ONE, D3D12_BLEND_ONE);
  line.DepthStencilState = DepthState(true, false, D3D12_COMPARISON_FUNC_LESS_EQUAL);
  if (FAILED(device->CreateGraphicsPipelineState(&line, IID_PPV_ARGS(&m_linePipeline))))
  {
    return false;
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC effect = line;
  effect.VS = Bytecode(g_EffectVS, sizeof(g_EffectVS));
  effect.PS = Bytecode(g_EffectPS, sizeof(g_EffectPS));
  if (FAILED(device->CreateGraphicsPipelineState(&effect, IID_PPV_ARGS(&m_effectPipeline))))
  {
    return false;
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC hud = base;
  hud.VS = Bytecode(g_HudVS, sizeof(g_HudVS));
  hud.PS = Bytecode(g_HudPS, sizeof(g_HudPS));
  hud.BlendState = BlendState(true, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA);
  hud.DepthStencilState = DepthState(false, false, D3D12_COMPARISON_FUNC_ALWAYS);
  return SUCCEEDED(device->CreateGraphicsPipelineState(&hud, IID_PPV_ARGS(&m_hudPipeline)));
}

bool Renderer::CreateShadowMap()
{
  ID3D12Device* device = m_device->Device();
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = SHADOW_MAP_SIZE;
  desc.Height = SHADOW_MAP_SIZE;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE clear{};
  clear.Format = DXGI_FORMAT_D32_FLOAT;
  clear.DepthStencil.Depth = 1.0f;
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                                             IID_PPV_ARGS(&m_shadowMap))))
  {
    return false;
  }
  m_shadowMap->SetName(L"ShadowMap");
  D3D12_DESCRIPTOR_HEAP_DESC dsvHeap{};
  dsvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsvHeap.NumDescriptors = 1;
  if (FAILED(device->CreateDescriptorHeap(&dsvHeap, IID_PPV_ARGS(&m_shadowDsvHeap))))
  {
    return false;
  }
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
  dsv.Format = DXGI_FORMAT_D32_FLOAT;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  device->CreateDepthStencilView(m_shadowMap.Get(), &dsv, m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart());
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(m_shadowMap.Get(), &srv, m_shaderHeap->GetCPUDescriptorHandleForHeapStart());
  return true;
}

bool Renderer::SetMaterials(std::span<const GpuMaterial, MATERIAL_COUNT> _materials)
{
  m_device->WaitIdle();
  m_materials = m_device->CreateStaticBuffer(_materials.data(), _materials.size_bytes());
  return m_materials != nullptr;
}

bool Renderer::SetHudAtlas(std::uint32_t _width, std::uint32_t _height, std::span<const std::uint8_t> _coverage)
{
  if (_coverage.size() < static_cast<std::size_t>(_width) * _height)
  {
    return false;
  }
  m_device->WaitIdle();
  ComPtr<ID3D12Resource> atlas = m_device->CreateStaticTexture(DXGI_FORMAT_R8_UNORM, _width, _height, _coverage);
  if (!atlas)
  {
    return false;
  }
  atlas->SetName(L"HudFontAtlas");
  m_hudTextures[HUD_FONT_SLOT] = atlas;
  CreateHudView(HUD_FONT_SLOT, atlas.Get(), DXGI_FORMAT_R8_UNORM);
  return true;
}

bool Renderer::SetHudTexture(std::uint32_t _slot, const DdsImage& _image, std::string& _outError)
{
  if (_slot == HUD_FONT_SLOT || _slot >= HUD_TEXTURE_COUNT)
  {
    _outError = "HUD texture slot " + std::to_string(_slot) + " is not a sprite slot";
    return false;
  }
  if (!IsSupportedDdsFormat(_image.format) || _image.payload.size() != DdsPayloadBytes(_image.format, _image.width, _image.height))
  {
    _outError = "HUD texture payload does not match its format and size";
    return false;
  }
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support{_image.format, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE};
  constexpr D3D12_FORMAT_SUPPORT1 REQUIRED = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
  if (FAILED(m_device->Device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
      (support.Support1 & REQUIRED) != REQUIRED)
  {
    _outError = std::string("the device cannot sample ") + DdsFormatName(_image.format) + " as a 2D texture";
    return false;
  }
  m_device->WaitIdle();
  ComPtr<ID3D12Resource> texture = m_device->CreateStaticTexture(_image.format, _image.width, _image.height, _image.payload);
  if (!texture)
  {
    _outError = "HUD texture upload failed";
    return false;
  }
  texture->SetName(L"HudSpriteAtlas");
  m_hudTextures[_slot] = texture;
  CreateHudView(_slot, texture.Get(), _image.format);
  return true;
}

void Renderer::CreateHudView(std::uint32_t _slot, ID3D12Resource* _texture, DXGI_FORMAT _format)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = _format;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  D3D12_CPU_DESCRIPTOR_HANDLE handle = m_shaderHeap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(1 + _slot) * m_shaderDescriptorSize;
  m_device->Device()->CreateShaderResourceView(_texture, &srv, handle);
}

GpuVoxelModel Renderer::UploadModel(const VoxelSurface& _surface)
{
  std::vector<GpuVoxel> voxels;
  voxels.reserve(_surface.voxels.size());
  for (const SurfaceVoxel& voxel : _surface.voxels)
  {
    voxels.push_back(GpuVoxel{static_cast<std::uint32_t>(voxel.x) | (static_cast<std::uint32_t>(voxel.y) << 8) |
                                (static_cast<std::uint32_t>(voxel.z) << 16) | (static_cast<std::uint32_t>(voxel.colorIndex) << 24),
                              static_cast<std::uint32_t>(voxel.exposedFaceMask) | (static_cast<std::uint32_t>(voxel.faceOcclusion) << 8)});
  }
  GpuVoxelModel model;
  if (voxels.empty())
  {
    return model;
  }
  model.voxels = m_device->CreateStaticBuffer(voxels.data(), voxels.size() * sizeof(GpuVoxel));
  model.voxelCount = model.voxels ? static_cast<std::uint32_t>(voxels.size()) : 0;
  return model;
}

D3D12_GPU_VIRTUAL_ADDRESS Renderer::PushArray(const void* _data, std::uint64_t _bytes)
{
  const UploadRing::Allocation allocation = m_device->FrameUpload().Allocate(_bytes);
  if (allocation.cpu == nullptr)
  {
    return 0;
  }
  std::memcpy(allocation.cpu, _data, static_cast<std::size_t>(_bytes));
  return allocation.gpu;
}

void Renderer::BeginPass(const FrameConstants& _frame)
{
  ID3D12GraphicsCommandList* list = m_device->CommandList();
  list->SetGraphicsRootSignature(m_rootSignature.Get());
  ID3D12DescriptorHeap* heaps[] = {m_shaderHeap.Get()};
  list->SetDescriptorHeaps(1, heaps);
  list->SetGraphicsRootDescriptorTable(RootTextures, m_shaderHeap->GetGPUDescriptorHandleForHeapStart());
  m_frameConstants = m_device->FrameUpload().Push(_frame);
  list->SetGraphicsRootConstantBufferView(RootFrame, m_frameConstants);
  m_viewport = D3D12_VIEWPORT{_frame.viewportOrigin.x, _frame.viewportOrigin.y, _frame.viewportSize.x, _frame.viewportSize.y, 0.0f, 1.0f};
  m_scissor = D3D12_RECT{static_cast<LONG>(_frame.viewportOrigin.x), static_cast<LONG>(_frame.viewportOrigin.y),
                         static_cast<LONG>(_frame.viewportOrigin.x + _frame.viewportSize.x),
                         static_cast<LONG>(_frame.viewportOrigin.y + _frame.viewportSize.y)};
  list->RSSetViewports(1, &m_viewport);
  list->RSSetScissorRects(1, &m_scissor);
}

void Renderer::ClearDepthInViewport()
{
  m_device->CommandList()->ClearDepthStencilView(m_device->DepthView(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &m_scissor);
}

void Renderer::BindMainTargets()
{
  const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_device->BackBufferView();
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_device->DepthView();
  m_device->CommandList()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
}

void Renderer::ClearMainTargets(const float (&_linearColor)[4])
{
  ID3D12GraphicsCommandList* list = m_device->CommandList();
  list->ClearRenderTargetView(m_device->BackBufferView(), _linearColor, 0, nullptr);
  list->ClearDepthStencilView(m_device->DepthView(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void Renderer::BeginShadowPass(const FrameConstants& _frame)
{
  ID3D12GraphicsCommandList* list = m_device->CommandList();
  GraphicsDevice::Transition(list, m_shadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
  list->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
  list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  BeginPass(_frame);
  list->SetPipelineState(m_voxelShadowPipeline.Get());
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
}

void Renderer::EndShadowPass()
{
  GraphicsDevice::Transition(m_device->CommandList(), m_shadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  BindMainTargets();
}

void Renderer::DrawVoxelsShadow(const GpuVoxelModel& _model, const VoxelDrawConstants& _constants)
{
  if (_model.voxelCount == 0 || !m_materials)
  {
    return;
  }
  ID3D12GraphicsCommandList* list = m_device->CommandList();
  list->SetGraphicsRootConstantBufferView(RootDraw, m_device->FrameUpload().Push(_constants));
  list->SetGraphicsRootShaderResourceView(RootBufferA, _model.voxels->GetGPUVirtualAddress());
  list->SetGraphicsRootShaderResourceView(RootBufferB, m_materials->GetGPUVirtualAddress());
  list->DrawInstanced(4, _model.voxelCount, 0, 0);
}

void Renderer::DrawVoxels(const GpuVoxelModel& _model, const VoxelDrawConstants& _constants)
{
  if (_model.voxelCount == 0 || !m_materials)
  {
    return;
  }
  ID3D12GraphicsCommandList* list = m_device->CommandList();
  list->SetPipelineState(m_voxelPipeline.Get());
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  list->SetGraphicsRootConstantBufferView(RootDraw, m_device->FrameUpload().Push(_constants));
  list->SetGraphicsRootShaderResourceView(RootBufferA, _model.voxels->GetGPUVirtualAddress());
  list->SetGraphicsRootShaderResourceView(RootBufferB, m_materials->GetGPUVirtualAddress());
  list->DrawInstanced(4, _model.voxelCount, 0, 0);
}

void Renderer::DrawLines(std::span<const GpuLineSegment> _segments)
{
  if (_segments.empty())
  {
    return;
  }
  const D3D12_GPU_VIRTUAL_ADDRESS address = PushArray(_segments.data(), _segments.size_bytes());
  if (address == 0)
  {
    return;
  }
  ID3D12GraphicsCommandList* list = m_device->CommandList();
  list->SetPipelineState(m_linePipeline.Get());
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->SetGraphicsRootConstantBufferView(RootDraw, m_frameConstants);
  list->SetGraphicsRootShaderResourceView(RootBufferA, address);
  list->SetGraphicsRootShaderResourceView(RootBufferB, address);
  list->DrawInstanced(static_cast<UINT>(_segments.size() * 6), 1, 0, 0);
}

void Renderer::DrawEffects(std::span<const GpuEffect> _effects)
{
  if (_effects.empty())
  {
    return;
  }
  const D3D12_GPU_VIRTUAL_ADDRESS address = PushArray(_effects.data(), _effects.size_bytes());
  if (address == 0)
  {
    return;
  }
  ID3D12GraphicsCommandList* list = m_device->CommandList();
  list->SetPipelineState(m_effectPipeline.Get());
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->SetGraphicsRootConstantBufferView(RootDraw, m_frameConstants);
  list->SetGraphicsRootShaderResourceView(RootBufferA, address);
  list->SetGraphicsRootShaderResourceView(RootBufferB, address);
  list->DrawInstanced(static_cast<UINT>(_effects.size() * 6), 1, 0, 0);
}

void Renderer::DrawHud(std::span<const GpuHudVertex> _vertices)
{
  if (_vertices.empty())
  {
    return;
  }
  const D3D12_GPU_VIRTUAL_ADDRESS address = PushArray(_vertices.data(), _vertices.size_bytes());
  if (address == 0)
  {
    return;
  }
  ID3D12GraphicsCommandList* list = m_device->CommandList();
  list->SetPipelineState(m_hudPipeline.Get());
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->SetGraphicsRootConstantBufferView(RootDraw, m_frameConstants);
  list->SetGraphicsRootShaderResourceView(RootBufferA, address);
  list->SetGraphicsRootShaderResourceView(RootBufferB, address);
  list->DrawInstanced(static_cast<UINT>(_vertices.size()), 1, 0, 0);
}

} // namespace Neuron
