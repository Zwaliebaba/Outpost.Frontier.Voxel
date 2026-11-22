#include "VoxFile.h"

#include "FileSys.h"

#include <algorithm>
#include <cstring>

namespace Neuron
{

namespace
{

constexpr std::size_t CHUNK_HEADER_BYTES = 12;

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> _bytes, std::size_t _offset) noexcept
{
  return static_cast<std::uint32_t>(_bytes[_offset]) | (static_cast<std::uint32_t>(_bytes[_offset + 1]) << 8) |
         (static_cast<std::uint32_t>(_bytes[_offset + 2]) << 16) | (static_cast<std::uint32_t>(_bytes[_offset + 3]) << 24);
}

[[nodiscard]] bool IdEquals(std::span<const std::uint8_t> _bytes, std::size_t _offset, const char (&_id)[5]) noexcept
{
  return std::memcmp(_bytes.data() + _offset, _id, 4) == 0;
}

void AppendU32(std::vector<std::uint8_t>& _out, std::uint32_t _value)
{
  _out.push_back(static_cast<std::uint8_t>(_value & 0xFF));
  _out.push_back(static_cast<std::uint8_t>((_value >> 8) & 0xFF));
  _out.push_back(static_cast<std::uint8_t>((_value >> 16) & 0xFF));
  _out.push_back(static_cast<std::uint8_t>((_value >> 24) & 0xFF));
}

void AppendId(std::vector<std::uint8_t>& _out, const char (&_id)[5])
{
  _out.insert(_out.end(), _id, _id + 4);
}

struct ChunkHeader
{
  std::size_t contentOffset;
  std::size_t contentBytes;
  std::size_t childrenBytes;
};

[[nodiscard]] bool ReadMaterial(std::span<const std::uint8_t> _bytes, VoxMaterial& _out)
{
  if (_bytes.size() < 8)
  {
    return false;
  }
  _out.colorIndex = ReadU32(_bytes, 0);
  const std::uint32_t count = ReadU32(_bytes, 4);
  if (_out.colorIndex == 0 || _out.colorIndex > 255 || count > (_bytes.size() - 8) / 8)
  {
    return false;
  }
  std::size_t offset = 8;
  const auto readString = [&](std::string& _outString)
  {
    if (_bytes.size() - offset < 4)
    {
      return false;
    }
    const std::uint32_t size = ReadU32(_bytes, offset);
    offset += 4;
    if (size > _bytes.size() - offset)
    {
      return false;
    }
    _outString.assign(reinterpret_cast<const char*>(_bytes.data() + offset), size);
    offset += size;
    return true;
  };
  for (std::uint32_t i = 0; i < count; ++i)
  {
    std::string key;
    std::string value;
    if (!readString(key) || !readString(value) || key.empty() ||
        std::ranges::any_of(_out.properties, [&](const auto& _property) { return _property.first == key; }))
    {
      return false;
    }
    _out.properties.emplace_back(std::move(key), std::move(value));
  }
  return offset == _bytes.size();
}

// Reads a chunk header at _offset and checks that its content and children fit inside _end.
[[nodiscard]] bool ReadChunkHeader(std::span<const std::uint8_t> _bytes, std::size_t _offset, std::size_t _end, ChunkHeader& _out) noexcept
{
  if (_offset > _end || _end - _offset < CHUNK_HEADER_BYTES)
  {
    return false;
  }
  _out.contentBytes = ReadU32(_bytes, _offset + 4);
  _out.childrenBytes = ReadU32(_bytes, _offset + 8);
  _out.contentOffset = _offset + CHUNK_HEADER_BYTES;
  const std::size_t available = _end - _out.contentOffset;
  return _out.contentBytes <= available && _out.childrenBytes <= available - _out.contentBytes;
}

} // namespace

const char* VoxErrorName(VoxError _error) noexcept
{
  switch (_error)
  {
  case VoxError::None:
    return "None";
  case VoxError::BadMagic:
    return "BadMagic";
  case VoxError::UnsupportedVersion:
    return "UnsupportedVersion";
  case VoxError::TruncatedChunk:
    return "TruncatedChunk";
  case VoxError::MissingMainChunk:
    return "MissingMainChunk";
  case VoxError::MissingSize:
    return "MissingSize";
  case VoxError::MissingVoxels:
    return "MissingVoxels";
  case VoxError::InvalidDimensions:
    return "InvalidDimensions";
  case VoxError::VoxelOutOfBounds:
    return "VoxelOutOfBounds";
  case VoxError::InvalidMaterial:
    return "InvalidMaterial";
  case VoxError::FileIo:
    return "FileIo";
  }
  return "Unknown";
}

VoxPalette PaletteFromRgbaChunk(std::span<const std::uint8_t> _chunk) noexcept
{
  VoxPalette palette{};
  if (_chunk.size() < VOX_RGBA_CHUNK_BYTES)
  {
    return palette;
  }
  // Voxel colour index c lives at chunk entry c - 1. Index 0 is empty and stays transparent.
  for (std::size_t colorIndex = 1; colorIndex < palette.size(); ++colorIndex)
  {
    const std::size_t entry = (colorIndex - 1) * 4;
    palette[colorIndex] = Rgba8{_chunk[entry], _chunk[entry + 1], _chunk[entry + 2], _chunk[entry + 3]};
  }
  return palette;
}

std::vector<std::uint8_t> PaletteToRgbaChunk(const VoxPalette& _palette)
{
  std::vector<std::uint8_t> chunk(VOX_RGBA_CHUNK_BYTES, 0);
  // Chunk entry i is voxel colour index i + 1; the final entry is never addressed and stays zero.
  for (std::size_t entry = 0; entry + 1 < _palette.size(); ++entry)
  {
    const Rgba8& color = _palette[entry + 1];
    chunk[entry * 4 + 0] = color.r;
    chunk[entry * 4 + 1] = color.g;
    chunk[entry * 4 + 2] = color.b;
    chunk[entry * 4 + 3] = color.a;
  }
  return chunk;
}

VoxError ReadVox(std::span<const std::uint8_t> _bytes, VoxDocument& _outDocument)
{
  _outDocument = VoxDocument{};
  if (_bytes.size() < 8 || !IdEquals(_bytes, 0, "VOX "))
  {
    return VoxError::BadMagic;
  }
  const std::uint32_t version = ReadU32(_bytes, 4);
  if (version != 150 && version != 200)
  {
    return VoxError::UnsupportedVersion;
  }

  if (_bytes.size() < 8 + CHUNK_HEADER_BYTES)
  {
    return VoxError::TruncatedChunk;
  }
  if (!IdEquals(_bytes, 8, "MAIN"))
  {
    return VoxError::MissingMainChunk;
  }
  ChunkHeader mainChunk{};
  if (!ReadChunkHeader(_bytes, 8, _bytes.size(), mainChunk))
  {
    return VoxError::TruncatedChunk;
  }

  bool haveSize = false;
  bool haveVoxels = false;
  std::uint32_t sizeX = 0;
  std::uint32_t sizeY = 0;
  std::uint32_t sizeZ = 0;

  std::size_t offset = mainChunk.contentOffset + mainChunk.contentBytes;
  const std::size_t end = offset + mainChunk.childrenBytes;
  while (offset < end)
  {
    ChunkHeader chunk{};
    if (!ReadChunkHeader(_bytes, offset, end, chunk))
    {
      return VoxError::TruncatedChunk;
    }

    // Only the first model of a multi-model file is read; this repository writes one per file.
    if (IdEquals(_bytes, offset, "SIZE") && !haveSize)
    {
      if (chunk.contentBytes < 12)
      {
        return VoxError::TruncatedChunk;
      }
      sizeX = ReadU32(_bytes, chunk.contentOffset);
      sizeY = ReadU32(_bytes, chunk.contentOffset + 4);
      sizeZ = ReadU32(_bytes, chunk.contentOffset + 8);
      if (sizeX == 0 || sizeY == 0 || sizeZ == 0 || sizeX > VOX_MAX_DIMENSION || sizeY > VOX_MAX_DIMENSION || sizeZ > VOX_MAX_DIMENSION)
      {
        return VoxError::InvalidDimensions;
      }
      _outDocument.model = VoxModel(sizeX, sizeY, sizeZ);
      haveSize = true;
    }
    else if (IdEquals(_bytes, offset, "XYZI") && !haveVoxels)
    {
      if (!haveSize)
      {
        return VoxError::MissingSize;
      }
      if (chunk.contentBytes < 4)
      {
        return VoxError::TruncatedChunk;
      }
      const std::size_t voxelCount = ReadU32(_bytes, chunk.contentOffset);
      if (voxelCount > (chunk.contentBytes - 4) / 4)
      {
        return VoxError::TruncatedChunk;
      }
      for (std::size_t i = 0; i < voxelCount; ++i)
      {
        const std::size_t at = chunk.contentOffset + 4 + i * 4;
        const std::uint8_t x = _bytes[at];
        const std::uint8_t y = _bytes[at + 1];
        const std::uint8_t z = _bytes[at + 2];
        const std::uint8_t colorIndex = _bytes[at + 3];
        if (x >= sizeX || y >= sizeY || z >= sizeZ)
        {
          return VoxError::VoxelOutOfBounds;
        }
        // Index 0 is empty space by definition; a voxel that claims it is simply not there.
        _outDocument.model.Set(x, y, z, colorIndex);
      }
      haveVoxels = true;
    }
    else if (IdEquals(_bytes, offset, "RGBA"))
    {
      if (chunk.contentBytes < VOX_RGBA_CHUNK_BYTES)
      {
        return VoxError::TruncatedChunk;
      }
      const auto chunkBytes = _bytes.subspan(chunk.contentOffset, VOX_RGBA_CHUNK_BYTES);
      _outDocument.rgbaChunk.assign(chunkBytes.begin(), chunkBytes.end());
      _outDocument.palette = PaletteFromRgbaChunk(chunkBytes);
      _outDocument.hasPalette = true;
    }

    else if (IdEquals(_bytes, offset, "MATL"))
    {
      VoxMaterial material;
      if (!ReadMaterial(_bytes.subspan(chunk.contentOffset, chunk.contentBytes), material) ||
          std::ranges::any_of(_outDocument.materials,
                              [&](const VoxMaterial& _existing) { return _existing.colorIndex == material.colorIndex; }))
      {
        return VoxError::InvalidMaterial;
      }
      _outDocument.materials.push_back(std::move(material));
    }

    offset = chunk.contentOffset + chunk.contentBytes + chunk.childrenBytes;
  }

  if (!haveSize)
  {
    return VoxError::MissingSize;
  }
  if (!haveVoxels)
  {
    return VoxError::MissingVoxels;
  }
  return VoxError::None;
}

std::vector<std::uint8_t> WriteVox(const VoxModel& _model, const VoxPalette& _palette, std::span<const VoxMaterial> _materials)
{
  std::vector<std::uint8_t> voxels;
  voxels.reserve(_model.CountSolid() * 4);
  for (std::uint32_t z = 0; z < _model.SizeZ(); ++z)
  {
    for (std::uint32_t y = 0; y < _model.SizeY(); ++y)
    {
      for (std::uint32_t x = 0; x < _model.SizeX(); ++x)
      {
        const std::uint8_t colorIndex = _model.At(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
        if (colorIndex != VOX_EMPTY)
        {
          voxels.push_back(static_cast<std::uint8_t>(x));
          voxels.push_back(static_cast<std::uint8_t>(y));
          voxels.push_back(static_cast<std::uint8_t>(z));
          voxels.push_back(colorIndex);
        }
      }
    }
  }

  const std::vector<std::uint8_t> rgba = PaletteToRgbaChunk(_palette);
  const std::uint32_t sizeChunkBytes = static_cast<std::uint32_t>(CHUNK_HEADER_BYTES + 12);
  const std::uint32_t xyziChunkBytes = static_cast<std::uint32_t>(CHUNK_HEADER_BYTES + 4 + voxels.size());
  const std::uint32_t rgbaChunkBytes = static_cast<std::uint32_t>(CHUNK_HEADER_BYTES + rgba.size());

  std::vector<std::uint8_t> materialChunks;
  for (const VoxMaterial& material : _materials)
  {
    std::vector<std::uint8_t> content;
    AppendU32(content, material.colorIndex);
    AppendU32(content, static_cast<std::uint32_t>(material.properties.size()));
    for (const auto& [key, value] : material.properties)
    {
      for (const std::string* text : {&key, &value})
      {
        AppendU32(content, static_cast<std::uint32_t>(text->size()));
        content.insert(content.end(), text->begin(), text->end());
      }
    }
    AppendId(materialChunks, "MATL");
    AppendU32(materialChunks, static_cast<std::uint32_t>(content.size()));
    AppendU32(materialChunks, 0);
    materialChunks.insert(materialChunks.end(), content.begin(), content.end());
  }

  std::vector<std::uint8_t> out;
  out.reserve(8 + CHUNK_HEADER_BYTES + sizeChunkBytes + xyziChunkBytes + rgbaChunkBytes + materialChunks.size());
  AppendId(out, "VOX ");
  AppendU32(out, VOX_VERSION);

  AppendId(out, "MAIN");
  AppendU32(out, 0);
  AppendU32(out, sizeChunkBytes + xyziChunkBytes + rgbaChunkBytes + static_cast<std::uint32_t>(materialChunks.size()));

  AppendId(out, "SIZE");
  AppendU32(out, 12);
  AppendU32(out, 0);
  AppendU32(out, _model.SizeX());
  AppendU32(out, _model.SizeY());
  AppendU32(out, _model.SizeZ());

  AppendId(out, "XYZI");
  AppendU32(out, static_cast<std::uint32_t>(4 + voxels.size()));
  AppendU32(out, 0);
  AppendU32(out, static_cast<std::uint32_t>(voxels.size() / 4));
  out.insert(out.end(), voxels.begin(), voxels.end());

  AppendId(out, "RGBA");
  AppendU32(out, static_cast<std::uint32_t>(rgba.size()));
  AppendU32(out, 0);
  out.insert(out.end(), rgba.begin(), rgba.end());
  out.insert(out.end(), materialChunks.begin(), materialChunks.end());
  return out;
}

VoxError LoadVoxFile(const std::filesystem::path& _path, VoxDocument& _outDocument)
{
  std::vector<std::uint8_t> bytes;
  if (!ReadFileBytes(_path, bytes))
  {
    _outDocument = VoxDocument{};
    return VoxError::FileIo;
  }
  return ReadVox(bytes, _outDocument);
}

bool SaveVoxFile(const std::filesystem::path& _path, const VoxModel& _model, const VoxPalette& _palette,
                 std::span<const VoxMaterial> _materials)
{
  return WriteFileBytes(_path, WriteVox(_model, _palette, _materials));
}

} // namespace Neuron
