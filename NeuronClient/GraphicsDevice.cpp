#include "GraphicsDevice.h"

#include <d3d12sdklayers.h>

#include <algorithm>
#include <cstring>

namespace Neuron
{

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE _type) noexcept
{
  return D3D12_HEAP_PROPERTIES{.Type = _type,
                               .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                               .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                               .CreationNodeMask = 1,
                               .VisibleNodeMask = 1};
}

D3D12_RESOURCE_DESC BufferDescription(std::uint64_t _bytes) noexcept
{
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Alignment = 0;
  desc.Width = _bytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  return desc;
}

// ── UploadRing ───────────────────────────────────────────────────────────────────────────────

bool UploadRing::Create(ID3D12Device* _device, std::uint64_t _capacityBytes)
{
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC desc = BufferDescription(_capacityBytes);
  if (FAILED(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              IID_PPV_ARGS(&m_buffer))))
  {
    return false;
  }
  m_buffer->SetName(L"UploadRing");
  const D3D12_RANGE noRead{0, 0};
  void* mapped = nullptr;
  if (FAILED(m_buffer->Map(0, &noRead, &mapped)))
  {
    return false;
  }
  m_mapped = static_cast<std::uint8_t*>(mapped);
  m_capacity = _capacityBytes;
  m_offset = 0;
  return true;
}

UploadRing::Allocation UploadRing::Allocate(std::uint64_t _bytes) noexcept
{
  constexpr std::uint64_t ALIGNMENT = 256;
  const std::uint64_t start = (m_offset + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
  if (_bytes == 0 || start + _bytes > m_capacity)
  {
    return {};
  }
  m_offset = start + _bytes;
  return Allocation{m_mapped + start, m_buffer->GetGPUVirtualAddress() + start};
}

// ── GraphicsDevice ───────────────────────────────────────────────────────────────────────────

GraphicsDevice::~GraphicsDevice()
{
  if (m_device && m_queue && m_fence)
  {
    WaitIdle();
  }
  if (m_infoQueue && m_debugCallbackCookie != 0)
  {
    m_infoQueue->UnregisterMessageCallback(m_debugCallbackCookie);
  }
  if (m_fenceEvent != nullptr)
  {
    CloseHandle(m_fenceEvent);
  }
}

void GraphicsDevice::Transition(ID3D12GraphicsCommandList* _list, ID3D12Resource* _resource, D3D12_RESOURCE_STATES _before,
                                D3D12_RESOURCE_STATES _after) noexcept
{
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = _resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = _before;
  barrier.Transition.StateAfter = _after;
  _list->ResourceBarrier(1, &barrier);
}

bool GraphicsDevice::Initialize(const GraphicsDeviceDesc& _desc, std::string& _outError)
{
  m_width = std::max(1u, _desc.width);
  m_height = std::max(1u, _desc.height);

  UINT factoryFlags = 0;
  if (_desc.enableDebugLayer)
  {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
      debug->EnableDebugLayer();
      factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    else
    {
      OutputDebugStringW(L"Outpost: the D3D12 debug layer is not installed (Graphics Tools optional feature).\n");
    }
  }
  if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory))))
  {
    _outError = "CreateDXGIFactory2 failed";
    return false;
  }

  ComPtr<IDXGIAdapter1> adapter;
  if (_desc.useWarp)
  {
    if (FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))))
    {
      _outError = "no WARP adapter";
      return false;
    }
  }
  else
  {
    for (UINT i = 0;
         m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
      DXGI_ADAPTER_DESC1 adapterDesc{};
      adapter->GetDesc1(&adapterDesc);
      if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
          SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr)))
      {
        break;
      }
      adapter.Reset();
    }
    if (!adapter)
    {
      _outError = "no hardware adapter supports feature level 12_1";
      return false;
    }
  }
  DXGI_ADAPTER_DESC1 adapterDesc{};
  adapter->GetDesc1(&adapterDesc);
  m_adapterName = adapterDesc.Description;
  m_isWarp = (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

  if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device))))
  {
    _outError = "D3D12CreateDevice at feature level 12_1 failed";
    return false;
  }
  D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_7};
  if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) ||
      shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_7)
  {
    _outError = "the device does not support Shader Model 6.7";
    return false;
  }
  if (_desc.enableDebugLayer)
  {
    InstallDebugCallback();
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue))))
  {
    _outError = "CreateCommandQueue failed";
    return false;
  }

  DXGI_SWAP_CHAIN_DESC1 swapDesc{};
  swapDesc.Width = m_width;
  swapDesc.Height = m_height;
  swapDesc.Format = SWAP_CHAIN_FORMAT;
  swapDesc.SampleDesc.Count = 1;
  swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapDesc.BufferCount = BACK_BUFFER_COUNT;
  swapDesc.Scaling = DXGI_SCALING_STRETCH;
  swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  ComPtr<IDXGISwapChain1> swapChain;
  if (FAILED(m_factory->CreateSwapChainForHwnd(m_queue.Get(), _desc.window, &swapDesc, nullptr, nullptr, &swapChain)) ||
      FAILED(swapChain.As(&m_swapChain)))
  {
    _outError = "CreateSwapChainForHwnd failed";
    return false;
  }
  m_factory->MakeWindowAssociation(_desc.window, DXGI_MWA_NO_ALT_ENTER);

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.NumDescriptors = BACK_BUFFER_COUNT;
  D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
  dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsvHeapDesc.NumDescriptors = 1;
  if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))) ||
      FAILED(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))))
  {
    _outError = "CreateDescriptorHeap failed";
    return false;
  }
  m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  for (std::uint32_t i = 0; i < BACK_BUFFER_COUNT; ++i)
  {
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocators[i]))) ||
        !m_uploadRings[i].Create(m_device.Get(), UPLOAD_RING_BYTES))
    {
      _outError = "per-frame resources could not be created";
      return false;
    }
  }
  if (FAILED(
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList))) ||
      FAILED(m_commandList->Close()) ||
      FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_immediateAllocator))) ||
      FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_immediateAllocator.Get(), nullptr,
                                         IID_PPV_ARGS(&m_immediateList))) ||
      FAILED(m_immediateList->Close()))
  {
    _outError = "CreateCommandList failed";
    return false;
  }
  if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
  {
    _outError = "CreateFence failed";
    return false;
  }
  m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (m_fenceEvent == nullptr)
  {
    _outError = "CreateEvent failed";
    return false;
  }
  if (!CreateTimestampResources())
  {
    _outError = "timestamp queries could not be created";
    return false;
  }
  if (!CreateSizeDependentResources())
  {
    _outError = "swap-chain resources could not be created";
    return false;
  }
  return true;
}

void GraphicsDevice::InstallDebugCallback()
{
  ComPtr<ID3D12InfoQueue> infoQueue;
  if (SUCCEEDED(m_device.As(&infoQueue)) && IsDebuggerPresent())
  {
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
  }
  if (SUCCEEDED(m_device.As(&m_infoQueue)))
  {
    m_infoQueue->RegisterMessageCallback(&GraphicsDevice::DebugMessageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this,
                                         &m_debugCallbackCookie);
  }
}

void CALLBACK GraphicsDevice::DebugMessageCallback(D3D12_MESSAGE_CATEGORY /*_category*/, D3D12_MESSAGE_SEVERITY _severity,
                                                   D3D12_MESSAGE_ID /*_id*/, LPCSTR _description, void* _context)
{
  auto* self = static_cast<GraphicsDevice*>(_context);
  const char* label = "INFO";
  if (_severity == D3D12_MESSAGE_SEVERITY_CORRUPTION || _severity == D3D12_MESSAGE_SEVERITY_ERROR)
  {
    ++self->m_debugErrorCount;
    label = "ERROR";
  }
  else if (_severity == D3D12_MESSAGE_SEVERITY_WARNING)
  {
    ++self->m_debugWarningCount;
    label = "WARNING";
  }
  else
  {
    return;
  }
  std::string line = std::string("D3D12 ") + label + ": " + (_description != nullptr ? _description : "");
  OutputDebugStringA((line + "\n").c_str());
  if (self->m_debugMessages.size() < 64)
  {
    self->m_debugMessages.push_back(std::move(line));
  }
}

bool GraphicsDevice::CreateSizeDependentResources()
{
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
  rtvDesc.Format = RENDER_TARGET_FORMAT;
  rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  for (std::uint32_t i = 0; i < BACK_BUFFER_COUNT; ++i)
  {
    if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]))))
    {
      return false;
    }
    m_backBuffers[i]->SetName(L"BackBuffer");
    m_device->CreateRenderTargetView(m_backBuffers[i].Get(), &rtvDesc, rtv);
    rtv.ptr += m_rtvDescriptorSize;
  }

  D3D12_RESOURCE_DESC depthDesc{};
  depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depthDesc.Width = m_width;
  depthDesc.Height = m_height;
  depthDesc.DepthOrArraySize = 1;
  depthDesc.MipLevels = 1;
  depthDesc.Format = DEPTH_FORMAT;
  depthDesc.SampleDesc.Count = 1;
  depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE clear{};
  clear.Format = DEPTH_FORMAT;
  clear.DepthStencil.Depth = 1.0f;
  const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                               IID_PPV_ARGS(&m_depthBuffer))))
  {
    return false;
  }
  m_depthBuffer->SetName(L"DepthBuffer");
  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
  dsvDesc.Format = DEPTH_FORMAT;
  dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

  m_readbackRowPitch = (static_cast<std::uint64_t>(m_width) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                       ~static_cast<std::uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
  const D3D12_HEAP_PROPERTIES readbackHeap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
  const D3D12_RESOURCE_DESC readbackDesc = BufferDescription(m_readbackRowPitch * m_height);
  if (FAILED(m_device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&m_readback))))
  {
    return false;
  }
  m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
  return true;
}

void GraphicsDevice::ReleaseSizeDependentResources() noexcept
{
  for (auto& buffer : m_backBuffers)
  {
    buffer.Reset();
  }
  m_depthBuffer.Reset();
  m_readback.Reset();
}

bool GraphicsDevice::Resize(std::uint32_t _width, std::uint32_t _height)
{
  if (_width == 0 || _height == 0 || (_width == m_width && _height == m_height))
  {
    return true;
  }
  WaitIdle();
  ReleaseSizeDependentResources();
  if (FAILED(m_swapChain->ResizeBuffers(BACK_BUFFER_COUNT, _width, _height, SWAP_CHAIN_FORMAT, 0)))
  {
    return false;
  }
  m_width = _width;
  m_height = _height;
  return CreateSizeDependentResources();
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsDevice::BackBufferView() const noexcept
{
  D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(m_backBufferIndex) * m_rtvDescriptorSize;
  return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsDevice::DepthView() const noexcept
{
  return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

ID3D12GraphicsCommandList* GraphicsDevice::BeginFrame()
{
  m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
  const std::uint64_t waitFor = m_frameFenceValues[m_backBufferIndex];
  if (m_fence->GetCompletedValue() < waitFor)
  {
    m_fence->SetEventOnCompletion(waitFor, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }
  ReadCompletedTimestamps();
  m_allocators[m_backBufferIndex]->Reset();
  m_commandList->Reset(m_allocators[m_backBufferIndex].Get(), nullptr);
  m_uploadRings[m_backBufferIndex].Reset();
  m_timestampCounts[m_backBufferIndex] = 0;
  Timestamp();

  Transition(m_commandList.Get(), m_backBuffers[m_backBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  const D3D12_CPU_DESCRIPTOR_HANDLE rtv = BackBufferView();
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = DepthView();
  m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
  return m_commandList.Get();
}

bool GraphicsDevice::EndFrame(bool _captureBackBuffer, bool _vsync)
{
  ID3D12Resource* backBuffer = m_backBuffers[m_backBufferIndex].Get();
  if (_captureBackBuffer)
  {
    Transition(m_commandList.Get(), backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = m_readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Offset = 0;
    destination.PlacedFootprint.Footprint.Format = SWAP_CHAIN_FORMAT;
    destination.PlacedFootprint.Footprint.Width = m_width;
    destination.PlacedFootprint.Footprint.Height = m_height;
    destination.PlacedFootprint.Footprint.Depth = 1;
    destination.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(m_readbackRowPitch);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = backBuffer;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    m_commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    Transition(m_commandList.Get(), backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
  }
  else
  {
    Transition(m_commandList.Get(), backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  }
  Timestamp();
  const std::uint32_t firstQuery = m_backBufferIndex * MAX_TIMESTAMPS_PER_FRAME;
  m_commandList->ResolveQueryData(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, firstQuery, m_timestampCounts[m_backBufferIndex],
                                  m_timestampReadback.Get(), std::uint64_t{firstQuery} * sizeof(std::uint64_t));
  if (FAILED(m_commandList->Close()))
  {
    return false;
  }
  ID3D12CommandList* lists[] = {m_commandList.Get()};
  m_queue->ExecuteCommandLists(1, lists);

  const HRESULT presented = m_swapChain->Present(_vsync ? 1 : 0, 0);
  m_frameFenceValues[m_backBufferIndex] = m_nextFenceValue;
  m_queue->Signal(m_fence.Get(), m_nextFenceValue++);
  if (presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET || FAILED(m_device->GetDeviceRemovedReason()))
  {
    return false;
  }

  if (_captureBackBuffer)
  {
    WaitIdle();
    const D3D12_RANGE readRange{0, static_cast<SIZE_T>(m_readbackRowPitch * m_height)};
    void* mapped = nullptr;
    if (SUCCEEDED(m_readback->Map(0, &readRange, &mapped)))
    {
      m_capturedWidth = m_width;
      m_capturedHeight = m_height;
      m_capturedPixels.resize(static_cast<std::size_t>(m_width) * m_height * 4);
      for (std::uint32_t row = 0; row < m_height; ++row)
      {
        std::memcpy(m_capturedPixels.data() + static_cast<std::size_t>(row) * m_width * 4,
                    static_cast<const std::uint8_t*>(mapped) + row * m_readbackRowPitch, static_cast<std::size_t>(m_width) * 4);
      }
      const D3D12_RANGE noWrite{0, 0};
      m_readback->Unmap(0, &noWrite);
    }
  }
  return true;
}

bool GraphicsDevice::CreateTimestampResources()
{
  D3D12_QUERY_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  heapDesc.Count = MAX_TIMESTAMPS_PER_FRAME * BACK_BUFFER_COUNT;
  if (FAILED(m_device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_timestampHeap))) ||
      FAILED(m_queue->GetTimestampFrequency(&m_timestampFrequency)) || m_timestampFrequency == 0)
  {
    return false;
  }
  const D3D12_HEAP_PROPERTIES readbackHeap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
  const D3D12_RESOURCE_DESC readbackDesc = BufferDescription(std::uint64_t{heapDesc.Count} * sizeof(std::uint64_t));
  return SUCCEEDED(m_device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                     nullptr, IID_PPV_ARGS(&m_timestampReadback)));
}

void GraphicsDevice::Timestamp()
{
  std::uint32_t& count = m_timestampCounts[m_backBufferIndex];
  if (count < MAX_TIMESTAMPS_PER_FRAME)
  {
    m_commandList->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_backBufferIndex * MAX_TIMESTAMPS_PER_FRAME + count);
    ++count;
  }
}

void GraphicsDevice::ReadCompletedTimestamps()
{
  // Called once this back buffer's last frame has finished on the GPU, so its resolved
  // timestamps are final.
  const std::uint32_t count = m_timestampCounts[m_backBufferIndex];
  if (count < 2)
  {
    return;
  }
  const std::uint32_t firstQuery = m_backBufferIndex * MAX_TIMESTAMPS_PER_FRAME;
  const D3D12_RANGE readRange{firstQuery * sizeof(std::uint64_t), (firstQuery + count) * sizeof(std::uint64_t)};
  void* mapped = nullptr;
  if (FAILED(m_timestampReadback->Map(0, &readRange, &mapped)))
  {
    return;
  }
  const auto* ticks = static_cast<const std::uint64_t*>(mapped) + firstQuery;
  m_completedTimestampsMilliseconds.resize(count);
  for (std::uint32_t i = 0; i < count; ++i)
  {
    const std::uint64_t elapsed = ticks[i] >= ticks[0] ? ticks[i] - ticks[0] : 0;
    m_completedTimestampsMilliseconds[i] = static_cast<double>(elapsed) * 1000.0 / static_cast<double>(m_timestampFrequency);
  }
  const D3D12_RANGE noWrite{0, 0};
  m_timestampReadback->Unmap(0, &noWrite);
}

void GraphicsDevice::WaitIdle()
{
  const std::uint64_t value = m_nextFenceValue++;
  if (FAILED(m_queue->Signal(m_fence.Get(), value)))
  {
    return;
  }
  if (m_fence->GetCompletedValue() < value)
  {
    m_fence->SetEventOnCompletion(value, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }
}

bool GraphicsDevice::ExecuteImmediate(ID3D12Resource* _uploadSource, ID3D12Resource* _destination, bool _isTexture,
                                      const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& _footprint, D3D12_RESOURCE_STATES _finalState)
{
  if (FAILED(m_immediateAllocator->Reset()) || FAILED(m_immediateList->Reset(m_immediateAllocator.Get(), nullptr)))
  {
    return false;
  }
  if (_isTexture)
  {
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = _destination;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = _uploadSource;
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = _footprint;
    m_immediateList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  }
  else
  {
    m_immediateList->CopyBufferRegion(_destination, 0, _uploadSource, 0, _footprint.Footprint.Width);
  }
  Transition(m_immediateList.Get(), _destination, D3D12_RESOURCE_STATE_COPY_DEST, _finalState);
  if (FAILED(m_immediateList->Close()))
  {
    return false;
  }
  ID3D12CommandList* lists[] = {m_immediateList.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  WaitIdle();
  return true;
}

ComPtr<ID3D12Resource> GraphicsDevice::CreateStaticBuffer(const void* _data, std::uint64_t _bytes)
{
  ComPtr<ID3D12Resource> upload;
  ComPtr<ID3D12Resource> buffer;
  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC desc = BufferDescription(std::max<std::uint64_t>(_bytes, 16));
  if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&upload))) ||
      FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               IID_PPV_ARGS(&buffer))))
  {
    return nullptr;
  }
  void* mapped = nullptr;
  const D3D12_RANGE noRead{0, 0};
  if (FAILED(upload->Map(0, &noRead, &mapped)))
  {
    return nullptr;
  }
  std::memcpy(mapped, _data, static_cast<std::size_t>(_bytes));
  upload->Unmap(0, nullptr);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  footprint.Footprint.Width = static_cast<UINT>(_bytes);
  if (!ExecuteImmediate(upload.Get(), buffer.Get(), false, footprint,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
  {
    return nullptr;
  }
  return buffer;
}

ComPtr<ID3D12Resource> GraphicsDevice::CreateStaticTexture(DXGI_FORMAT _format, std::uint32_t _width, std::uint32_t _height,
                                                           std::uint32_t _bytesPerPixel, const void* _pixels)
{
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = _width;
  desc.Height = _height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = _format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  ComPtr<ID3D12Resource> texture;
  const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&texture))))
  {
    return nullptr;
  }
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows = 0;
  UINT64 rowBytes = 0;
  UINT64 totalBytes = 0;
  m_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);

  ComPtr<ID3D12Resource> upload;
  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC uploadDesc = BufferDescription(totalBytes);
  if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&upload))))
  {
    return nullptr;
  }
  void* mapped = nullptr;
  const D3D12_RANGE noRead{0, 0};
  if (FAILED(upload->Map(0, &noRead, &mapped)))
  {
    return nullptr;
  }
  for (UINT row = 0; row < rows; ++row)
  {
    std::memcpy(static_cast<std::uint8_t*>(mapped) + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                static_cast<const std::uint8_t*>(_pixels) + static_cast<std::size_t>(row) * _width * _bytesPerPixel,
                static_cast<std::size_t>(_width) * _bytesPerPixel);
  }
  upload->Unmap(0, nullptr);
  if (!ExecuteImmediate(upload.Get(), texture.Get(), true, footprint, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
  {
    return nullptr;
  }
  return texture;
}

} // namespace Neuron
