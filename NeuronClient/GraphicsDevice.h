#pragma once

#include "WindowsSdk.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Neuron
{

using Microsoft::WRL::ComPtr;

// Hand-written replacements for the d3dx12.h helpers, which are not Windows SDK content (R14).
[[nodiscard]] D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE _type) noexcept;
[[nodiscard]] D3D12_RESOURCE_DESC BufferDescription(std::uint64_t _bytes) noexcept;

/// A per-frame linear allocator in a persistently mapped upload buffer. Everything written here
/// lives until the frame that wrote it has finished on the GPU.
class UploadRing
{
public:
  struct Allocation
  {
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
  };

  [[nodiscard]] bool Create(ID3D12Device* _device, std::uint64_t _capacityBytes);
  void Reset() noexcept
  {
    m_offset = 0;
  }

  /// Returns an allocation aligned to 256 bytes (constant-buffer and root-descriptor alignment),
  /// or an empty one when the ring is full.
  [[nodiscard]] Allocation Allocate(std::uint64_t _bytes) noexcept;

  template <typename T> [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS Push(const T& _value) noexcept
  {
    const Allocation allocation = Allocate(sizeof(T));
    if (allocation.cpu != nullptr)
    {
      *static_cast<T*>(allocation.cpu) = _value;
    }
    return allocation.gpu;
  }

private:
  ComPtr<ID3D12Resource> m_buffer;
  std::uint8_t* m_mapped = nullptr;
  std::uint64_t m_capacity = 0;
  std::uint64_t m_offset = 0;
};

struct GraphicsDeviceDesc
{
  HWND window = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool useWarp = false;
  bool enableDebugLayer = false;
};

/// The Direct3D 12 device, its queue, the swap chain and depth buffer, and frame pacing
/// (ADR-003). Each of the BACK_BUFFER_COUNT back buffers has its own command allocator and upload
/// ring: before a back buffer is reused, the fence value its last frame signalled is waited on.
class GraphicsDevice
{
public:
  static constexpr std::uint32_t BACK_BUFFER_COUNT = 3;
  static constexpr DXGI_FORMAT SWAP_CHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
  static constexpr DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  static constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
  static constexpr std::uint64_t UPLOAD_RING_BYTES = 48ull * 1024 * 1024;

  GraphicsDevice() = default;
  GraphicsDevice(const GraphicsDevice&) = delete;
  GraphicsDevice& operator=(const GraphicsDevice&) = delete;
  ~GraphicsDevice();

  [[nodiscard]] bool Initialize(const GraphicsDeviceDesc& _desc, std::string& _outError);

  /// Waits for the GPU, then resizes the swap chain and recreates the depth buffer.
  [[nodiscard]] bool Resize(std::uint32_t _width, std::uint32_t _height);

  /// Begins a frame: waits until this back buffer is free, resets its allocator and the command
  /// list, transitions the back buffer to RENDER_TARGET and binds it with the depth buffer.
  [[nodiscard]] ID3D12GraphicsCommandList* BeginFrame();

  /// Ends the frame: optionally copies the back buffer for capture, transitions it to PRESENT,
  /// submits and presents. Returns false when the device was removed.
  [[nodiscard]] bool EndFrame(bool _captureBackBuffer, bool _vsync);

  void WaitIdle();

  /// Default-heap buffer filled through a one-off upload; the call waits for the copy.
  [[nodiscard]] ComPtr<ID3D12Resource> CreateStaticBuffer(const void* _data, std::uint64_t _bytes);

  /// Default-heap 2D texture with one mip, filled through a one-off upload.
  [[nodiscard]] ComPtr<ID3D12Resource> CreateStaticTexture(DXGI_FORMAT _format, std::uint32_t _width, std::uint32_t _height,
                                                           std::uint32_t _bytesPerPixel, const void* _pixels);

  [[nodiscard]] ID3D12Device* Device() const noexcept
  {
    return m_device.Get();
  }
  [[nodiscard]] ID3D12GraphicsCommandList* CommandList() const noexcept
  {
    return m_commandList.Get();
  }
  [[nodiscard]] UploadRing& FrameUpload() noexcept
  {
    return m_uploadRings[m_backBufferIndex];
  }
  [[nodiscard]] std::uint32_t Width() const noexcept
  {
    return m_width;
  }
  [[nodiscard]] std::uint32_t Height() const noexcept
  {
    return m_height;
  }
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE BackBufferView() const noexcept;
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE DepthView() const noexcept;
  [[nodiscard]] const std::wstring& AdapterName() const noexcept
  {
    return m_adapterName;
  }
  [[nodiscard]] bool IsWarp() const noexcept
  {
    return m_isWarp;
  }

  /// The last captured back buffer as tightly packed RGBA8 (sRGB-encoded), top row first.
  [[nodiscard]] const std::vector<std::uint8_t>& CapturedPixels() const noexcept
  {
    return m_capturedPixels;
  }
  [[nodiscard]] std::uint32_t CapturedWidth() const noexcept
  {
    return m_capturedWidth;
  }
  [[nodiscard]] std::uint32_t CapturedHeight() const noexcept
  {
    return m_capturedHeight;
  }

  /// Debug-layer messages of severity ERROR or CORRUPTION seen so far (zero without the layer).
  [[nodiscard]] std::uint32_t DebugErrorCount() const noexcept
  {
    return m_debugErrorCount;
  }
  [[nodiscard]] std::uint32_t DebugWarningCount() const noexcept
  {
    return m_debugWarningCount;
  }
  [[nodiscard]] const std::vector<std::string>& DebugMessages() const noexcept
  {
    return m_debugMessages;
  }

  static void Transition(ID3D12GraphicsCommandList* _list, ID3D12Resource* _resource, D3D12_RESOURCE_STATES _before,
                         D3D12_RESOURCE_STATES _after) noexcept;

private:
  [[nodiscard]] bool CreateSizeDependentResources();
  void ReleaseSizeDependentResources() noexcept;
  void InstallDebugCallback();
  [[nodiscard]] bool ExecuteImmediate(ID3D12Resource* _uploadSource, ID3D12Resource* _destination, bool _isTexture,
                                      const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& _footprint, D3D12_RESOURCE_STATES _finalState);
  static void CALLBACK DebugMessageCallback(D3D12_MESSAGE_CATEGORY _category, D3D12_MESSAGE_SEVERITY _severity, D3D12_MESSAGE_ID _id,
                                            LPCSTR _description, void* _context);

  ComPtr<IDXGIFactory6> m_factory;
  ComPtr<ID3D12Device> m_device;
  ComPtr<ID3D12CommandQueue> m_queue;
  ComPtr<IDXGISwapChain3> m_swapChain;
  ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
  ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
  std::array<ComPtr<ID3D12Resource>, BACK_BUFFER_COUNT> m_backBuffers;
  ComPtr<ID3D12Resource> m_depthBuffer;
  std::array<ComPtr<ID3D12CommandAllocator>, BACK_BUFFER_COUNT> m_allocators;
  ComPtr<ID3D12GraphicsCommandList> m_commandList;
  ComPtr<ID3D12CommandAllocator> m_immediateAllocator;
  ComPtr<ID3D12GraphicsCommandList> m_immediateList;
  ComPtr<ID3D12Fence> m_fence;
  HANDLE m_fenceEvent = nullptr;
  std::uint64_t m_nextFenceValue = 1;
  std::array<std::uint64_t, BACK_BUFFER_COUNT> m_frameFenceValues{};
  std::array<UploadRing, BACK_BUFFER_COUNT> m_uploadRings;
  ComPtr<ID3D12Resource> m_readback;
  std::uint64_t m_readbackRowPitch = 0;
  std::vector<std::uint8_t> m_capturedPixels;
  std::uint32_t m_capturedWidth = 0;
  std::uint32_t m_capturedHeight = 0;
  std::uint32_t m_rtvDescriptorSize = 0;
  std::uint32_t m_backBufferIndex = 0;
  std::uint32_t m_width = 0;
  std::uint32_t m_height = 0;
  std::wstring m_adapterName;
  bool m_isWarp = false;
  DWORD m_debugCallbackCookie = 0;
  ComPtr<ID3D12InfoQueue1> m_infoQueue;
  std::uint32_t m_debugErrorCount = 0;
  std::uint32_t m_debugWarningCount = 0;
  std::vector<std::string> m_debugMessages;
};

} // namespace Neuron
