#pragma once

#include "WindowsSdk.h"

#include <dxgiformat.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

// DirectDraw Surface files, read by hand (ADR-023). Only the subset the UI ships is accepted:
//
//   "DDS " magic, the 124-byte DDS_HEADER, the 20-byte DDS_HEADER_DXT10, then one payload.
//
// A 2D texture with an array size of one and exactly one mip level, in R8_UNORM, R8G8B8A8_UNORM,
// R8G8B8A8_UNORM_SRGB, BC7_UNORM or BC7_UNORM_SRGB. Rows are tightly packed, the payload's length
// is checked exactly, and BC7 is validated and handed to the GPU, never decoded here. Legacy
// (non-DX10) headers, cube maps, arrays, volumes and mip chains are refused, each with a reason.
// Integers in the file are little-endian.

inline constexpr std::uint32_t DDS_MAGIC = 0x20534444u; // "DDS "
inline constexpr std::size_t DDS_HEADER_BYTES = 124;
inline constexpr std::size_t DDS_DX10_HEADER_BYTES = 20;
inline constexpr std::size_t DDS_PAYLOAD_OFFSET = 4 + DDS_HEADER_BYTES + DDS_DX10_HEADER_BYTES;
/// D3D12's largest 2D texture dimension (D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION).
inline constexpr std::uint32_t DDS_MAX_DIMENSION = 16384;

/// The alpha convention a DX10 header declares in the low three bits of miscFlags2.
enum class DdsAlphaMode : std::uint8_t
{
  Unknown = 0,
  Straight = 1,
  Premultiplied = 2,
  Opaque = 3,
  Custom = 4
};

/// One decoded DDS: its format, size, declared alpha mode and the payload exactly as the GPU
/// takes it, rows tightly packed (block rows for BC7).
struct DdsImage
{
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  DdsAlphaMode alphaMode = DdsAlphaMode::Unknown;
  std::vector<std::uint8_t> payload;
};

/// Whether ParseDds accepts the format at all.
[[nodiscard]] bool IsSupportedDdsFormat(DXGI_FORMAT _format) noexcept;

/// Whether a format carries colour (and therefore straight alpha) rather than R8 coverage.
[[nodiscard]] bool IsDdsColorFormat(DXGI_FORMAT _format) noexcept;

/// Whether a format's colour is sRGB-encoded.
[[nodiscard]] bool IsDdsSrgbFormat(DXGI_FORMAT _format) noexcept;

/// The format's name as dxgiformat.h spells it without the DXGI_FORMAT_ prefix, for messages.
[[nodiscard]] const char* DdsFormatName(DXGI_FORMAT _format) noexcept;

/// Bytes of one tightly packed row (one row of 4x4 blocks for BC7), and the number of such rows.
/// Both are 0 for an unsupported format.
[[nodiscard]] std::uint64_t DdsRowBytes(DXGI_FORMAT _format, std::uint32_t _width) noexcept;
[[nodiscard]] std::uint32_t DdsRowCount(DXGI_FORMAT _format, std::uint32_t _height) noexcept;

/// The exact payload length of a one-mip 2D texture: DdsRowBytes * DdsRowCount.
[[nodiscard]] std::uint64_t DdsPayloadBytes(DXGI_FORMAT _format, std::uint32_t _width, std::uint32_t _height) noexcept;

/// Parses a whole file held in memory. On refusal returns false with the reason in _outReason and
/// leaves _outImage unspecified.
[[nodiscard]] bool ParseDds(std::span<const std::uint8_t> _file, DdsImage& _outImage, std::string& _outReason);

/// Serializes an image in exactly the subset ParseDds reads, with the DX10 header. Used by tests
/// and tools; the shipped atlases come from Tools/BuildUiAtlases.py, which writes the same bytes.
[[nodiscard]] std::vector<std::uint8_t> WriteDds(const DdsImage& _image);

} // namespace Neuron
