#include "DdsFile.h"

#include <algorithm>

namespace Neuron
{

namespace
{

// DDS_HEADER, as byte offsets from the header's start (the file's byte 4).
constexpr std::size_t HEADER_SIZE = 0;
constexpr std::size_t HEADER_FLAGS = 4;
constexpr std::size_t HEADER_HEIGHT = 8;
constexpr std::size_t HEADER_WIDTH = 12;
constexpr std::size_t HEADER_PITCH = 16;
constexpr std::size_t HEADER_DEPTH = 20;
constexpr std::size_t HEADER_MIP_COUNT = 24;
constexpr std::size_t PIXEL_FORMAT_SIZE = 72;
constexpr std::size_t PIXEL_FORMAT_FLAGS = 76;
constexpr std::size_t PIXEL_FORMAT_FOUR_CC = 80;
constexpr std::size_t HEADER_CAPS = 104;
constexpr std::size_t HEADER_CAPS2 = 108;

// DDS_HEADER_DXT10, as byte offsets from its start (the file's byte 128).
constexpr std::size_t DX10_FORMAT = 0;
constexpr std::size_t DX10_DIMENSION = 4;
constexpr std::size_t DX10_MISC_FLAG = 8;
constexpr std::size_t DX10_ARRAY_SIZE = 12;
constexpr std::size_t DX10_MISC_FLAGS2 = 16;

constexpr std::uint32_t PIXEL_FORMAT_BYTES = 32;
constexpr std::uint32_t DDSD_CAPS = 0x1u;
constexpr std::uint32_t DDSD_HEIGHT = 0x2u;
constexpr std::uint32_t DDSD_WIDTH = 0x4u;
constexpr std::uint32_t DDSD_PITCH = 0x8u;
constexpr std::uint32_t DDSD_PIXELFORMAT = 0x1000u;
constexpr std::uint32_t DDSD_MIPMAPCOUNT = 0x20000u;
constexpr std::uint32_t DDSD_LINEARSIZE = 0x80000u;
constexpr std::uint32_t DDSD_DEPTH = 0x800000u;
constexpr std::uint32_t DDSD_REQUIRED = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
constexpr std::uint32_t DDPF_FOURCC = 0x4u;
constexpr std::uint32_t FOUR_CC_DX10 = 0x30315844u; // "DX10"
constexpr std::uint32_t DDSCAPS_TEXTURE = 0x1000u;
constexpr std::uint32_t DDSCAPS2_CUBEMAP = 0x200u;
constexpr std::uint32_t DDSCAPS2_VOLUME = 0x200000u;
constexpr std::uint32_t DIMENSION_TEXTURE2D = 3;
constexpr std::uint32_t MISC_TEXTURECUBE = 0x4u;
constexpr std::uint32_t ALPHA_MODE_MASK = 0x7u;
constexpr std::uint32_t BC_BLOCK_TEXELS = 4;
constexpr std::uint64_t BC7_BLOCK_BYTES = 16;

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> _bytes, std::size_t _offset) noexcept
{
  return static_cast<std::uint32_t>(_bytes[_offset]) | (static_cast<std::uint32_t>(_bytes[_offset + 1]) << 8) |
         (static_cast<std::uint32_t>(_bytes[_offset + 2]) << 16) | (static_cast<std::uint32_t>(_bytes[_offset + 3]) << 24);
}

void WriteU32(std::vector<std::uint8_t>& _bytes, std::size_t _offset, std::uint32_t _value) noexcept
{
  for (std::size_t i = 0; i < 4; ++i)
  {
    _bytes[_offset + i] = static_cast<std::uint8_t>((_value >> (8 * i)) & 0xffu);
  }
}

[[nodiscard]] bool IsBlockCompressed(DXGI_FORMAT _format) noexcept
{
  return _format == DXGI_FORMAT_BC7_UNORM || _format == DXGI_FORMAT_BC7_UNORM_SRGB;
}

[[nodiscard]] std::uint32_t BytesPerTexel(DXGI_FORMAT _format) noexcept
{
  switch (_format)
  {
  case DXGI_FORMAT_R8_UNORM:
    return 1;
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    return 4;
  default:
    return 0;
  }
}

[[nodiscard]] const char* AlphaModeName(std::uint32_t _mode) noexcept
{
  switch (_mode)
  {
  case 0:
    return "unknown";
  case 1:
    return "straight";
  case 2:
    return "premultiplied";
  case 3:
    return "opaque";
  case 4:
    return "custom";
  default:
    return "invalid";
  }
}

} // namespace

bool IsSupportedDdsFormat(DXGI_FORMAT _format) noexcept
{
  return BytesPerTexel(_format) != 0 || IsBlockCompressed(_format);
}

bool IsDdsColorFormat(DXGI_FORMAT _format) noexcept
{
  return IsSupportedDdsFormat(_format) && _format != DXGI_FORMAT_R8_UNORM;
}

bool IsDdsSrgbFormat(DXGI_FORMAT _format) noexcept
{
  return _format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || _format == DXGI_FORMAT_BC7_UNORM_SRGB;
}

const char* DdsFormatName(DXGI_FORMAT _format) noexcept
{
  switch (_format)
  {
  case DXGI_FORMAT_R8_UNORM:
    return "R8_UNORM";
  case DXGI_FORMAT_R8G8B8A8_UNORM:
    return "R8G8B8A8_UNORM";
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    return "R8G8B8A8_UNORM_SRGB";
  case DXGI_FORMAT_BC7_UNORM:
    return "BC7_UNORM";
  case DXGI_FORMAT_BC7_UNORM_SRGB:
    return "BC7_UNORM_SRGB";
  default:
    return "an unsupported format";
  }
}

std::uint64_t DdsRowBytes(DXGI_FORMAT _format, std::uint32_t _width) noexcept
{
  if (IsBlockCompressed(_format))
  {
    const std::uint64_t blocks = std::max<std::uint64_t>(1, (static_cast<std::uint64_t>(_width) + BC_BLOCK_TEXELS - 1) / BC_BLOCK_TEXELS);
    return blocks * BC7_BLOCK_BYTES;
  }
  return static_cast<std::uint64_t>(_width) * BytesPerTexel(_format);
}

std::uint32_t DdsRowCount(DXGI_FORMAT _format, std::uint32_t _height) noexcept
{
  if (IsBlockCompressed(_format))
  {
    return std::max<std::uint32_t>(1, (_height + BC_BLOCK_TEXELS - 1) / BC_BLOCK_TEXELS);
  }
  return IsSupportedDdsFormat(_format) ? _height : 0;
}

std::uint64_t DdsPayloadBytes(DXGI_FORMAT _format, std::uint32_t _width, std::uint32_t _height) noexcept
{
  return DdsRowBytes(_format, _width) * DdsRowCount(_format, _height);
}

bool ParseDds(std::span<const std::uint8_t> _file, DdsImage& _outImage, std::string& _outReason)
{
  if (_file.size() < 4 || ReadU32(_file, 0) != DDS_MAGIC)
  {
    _outReason = "not a DDS file: the \"DDS \" magic is missing";
    return false;
  }
  if (_file.size() < 4 + DDS_HEADER_BYTES)
  {
    _outReason =
      "truncated header: " + std::to_string(_file.size()) + " bytes, the DDS header needs " + std::to_string(4 + DDS_HEADER_BYTES);
    return false;
  }
  const std::span<const std::uint8_t> header = _file.subspan(4, DDS_HEADER_BYTES);
  if (ReadU32(header, HEADER_SIZE) != DDS_HEADER_BYTES || ReadU32(header, PIXEL_FORMAT_SIZE) != PIXEL_FORMAT_BYTES)
  {
    _outReason = "invalid header: the header must declare 124 bytes and its pixel format 32";
    return false;
  }
  const std::uint32_t flags = ReadU32(header, HEADER_FLAGS);
  if ((flags & DDSD_REQUIRED) != DDSD_REQUIRED)
  {
    _outReason = "invalid header: the caps, width, height and pixel format flags are required";
    return false;
  }
  if ((ReadU32(header, HEADER_CAPS) & DDSCAPS_TEXTURE) == 0)
  {
    _outReason = "invalid header: the texture capability flag is missing";
    return false;
  }
  if ((ReadU32(header, PIXEL_FORMAT_FLAGS) & DDPF_FOURCC) == 0 || ReadU32(header, PIXEL_FORMAT_FOUR_CC) != FOUR_CC_DX10)
  {
    _outReason = "missing DX10 header: only DDS files with the DX10 extension header are read";
    return false;
  }
  const std::uint32_t caps2 = ReadU32(header, HEADER_CAPS2);
  if ((caps2 & DDSCAPS2_CUBEMAP) != 0)
  {
    _outReason = "cube maps are refused: only 2D textures are read";
    return false;
  }
  if ((caps2 & DDSCAPS2_VOLUME) != 0 || ((flags & DDSD_DEPTH) != 0 && ReadU32(header, HEADER_DEPTH) > 1))
  {
    _outReason = "volume textures are refused: only 2D textures are read";
    return false;
  }
  const std::uint32_t mipCount = (flags & DDSD_MIPMAPCOUNT) != 0 ? ReadU32(header, HEADER_MIP_COUNT) : 1;
  if (mipCount > 1)
  {
    _outReason = "mip count " + std::to_string(mipCount) + " is refused: UI atlases have exactly one mip level";
    return false;
  }
  if (_file.size() < DDS_PAYLOAD_OFFSET)
  {
    _outReason = "truncated header: the DX10 header needs " + std::to_string(DDS_PAYLOAD_OFFSET) + " bytes, the file has " +
                 std::to_string(_file.size());
    return false;
  }
  const std::span<const std::uint8_t> dx10 = _file.subspan(4 + DDS_HEADER_BYTES, DDS_DX10_HEADER_BYTES);
  const auto format = static_cast<DXGI_FORMAT>(ReadU32(dx10, DX10_FORMAT));
  if (!IsSupportedDdsFormat(format))
  {
    _outReason = "unsupported format " + std::to_string(ReadU32(dx10, DX10_FORMAT)) +
                 ": R8_UNORM, R8G8B8A8_UNORM(_SRGB) and BC7_UNORM(_SRGB) are read";
    return false;
  }
  if (ReadU32(dx10, DX10_DIMENSION) != DIMENSION_TEXTURE2D)
  {
    _outReason = "resource dimension " + std::to_string(ReadU32(dx10, DX10_DIMENSION)) + " is refused: only 2D textures are read";
    return false;
  }
  if ((ReadU32(dx10, DX10_MISC_FLAG) & MISC_TEXTURECUBE) != 0)
  {
    _outReason = "cube maps are refused: only 2D textures are read";
    return false;
  }
  if (const std::uint32_t arraySize = ReadU32(dx10, DX10_ARRAY_SIZE); arraySize != 1)
  {
    _outReason = "array size " + std::to_string(arraySize) + " is refused: only a single 2D texture is read";
    return false;
  }
  const std::uint32_t width = ReadU32(header, HEADER_WIDTH);
  const std::uint32_t height = ReadU32(header, HEADER_HEIGHT);
  if (width == 0 || height == 0 || width > DDS_MAX_DIMENSION || height > DDS_MAX_DIMENSION)
  {
    _outReason = "invalid size " + std::to_string(width) + " x " + std::to_string(height) + ": each side must be 1 to " +
                 std::to_string(DDS_MAX_DIMENSION);
    return false;
  }
  const std::uint32_t alphaBits = ReadU32(dx10, DX10_MISC_FLAGS2) & ALPHA_MODE_MASK;
  if (IsDdsColorFormat(format))
  {
    // Straight alpha is the only blend the HUD does (SRC_ALPHA / INV_SRC_ALPHA), so colour art must
    // say it is straight or opaque; premultiplied art would come out with dark fringes.
    if (alphaBits != static_cast<std::uint32_t>(DdsAlphaMode::Straight) && alphaBits != static_cast<std::uint32_t>(DdsAlphaMode::Opaque))
    {
      _outReason = std::string("alpha mode ") + AlphaModeName(alphaBits) + " is refused for " + DdsFormatName(format) +
                   ": colour art must declare straight or opaque alpha";
      return false;
    }
  }
  const std::uint64_t expected = DdsPayloadBytes(format, width, height);
  const std::uint64_t actual = _file.size() - DDS_PAYLOAD_OFFSET;
  if (actual != expected)
  {
    _outReason = std::string(actual < expected ? "truncated" : "over-long") + " payload: " + std::to_string(actual) + " bytes, " +
                 std::to_string(width) + " x " + std::to_string(height) + " " + DdsFormatName(format) + " needs exactly " +
                 std::to_string(expected);
    return false;
  }
  _outImage.format = format;
  _outImage.width = width;
  _outImage.height = height;
  _outImage.alphaMode = static_cast<DdsAlphaMode>(alphaBits);
  _outImage.payload.assign(_file.begin() + static_cast<std::ptrdiff_t>(DDS_PAYLOAD_OFFSET), _file.end());
  return true;
}

std::vector<std::uint8_t> WriteDds(const DdsImage& _image)
{
  std::vector<std::uint8_t> bytes(DDS_PAYLOAD_OFFSET, 0);
  const bool compressed = IsBlockCompressed(_image.format);
  WriteU32(bytes, 0, DDS_MAGIC);
  const std::size_t header = 4;
  WriteU32(bytes, header + HEADER_SIZE, static_cast<std::uint32_t>(DDS_HEADER_BYTES));
  WriteU32(bytes, header + HEADER_FLAGS, DDSD_REQUIRED | DDSD_MIPMAPCOUNT | (compressed ? DDSD_LINEARSIZE : DDSD_PITCH));
  WriteU32(bytes, header + HEADER_HEIGHT, _image.height);
  WriteU32(bytes, header + HEADER_WIDTH, _image.width);
  const std::uint64_t pitch =
    compressed ? DdsPayloadBytes(_image.format, _image.width, _image.height) : DdsRowBytes(_image.format, _image.width);
  WriteU32(bytes, header + HEADER_PITCH, static_cast<std::uint32_t>(pitch));
  WriteU32(bytes, header + HEADER_MIP_COUNT, 1);
  WriteU32(bytes, header + PIXEL_FORMAT_SIZE, PIXEL_FORMAT_BYTES);
  WriteU32(bytes, header + PIXEL_FORMAT_FLAGS, DDPF_FOURCC);
  WriteU32(bytes, header + PIXEL_FORMAT_FOUR_CC, FOUR_CC_DX10);
  WriteU32(bytes, header + HEADER_CAPS, DDSCAPS_TEXTURE);
  const std::size_t dx10 = 4 + DDS_HEADER_BYTES;
  WriteU32(bytes, dx10 + DX10_FORMAT, static_cast<std::uint32_t>(_image.format));
  WriteU32(bytes, dx10 + DX10_DIMENSION, DIMENSION_TEXTURE2D);
  WriteU32(bytes, dx10 + DX10_ARRAY_SIZE, 1);
  WriteU32(bytes, dx10 + DX10_MISC_FLAGS2, static_cast<std::uint32_t>(_image.alphaMode));
  bytes.insert(bytes.end(), _image.payload.begin(), _image.payload.end());
  return bytes;
}

} // namespace Neuron
