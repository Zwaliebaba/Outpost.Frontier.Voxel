#pragma once

#include "VoxModel.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Neuron
{

// MagicaVoxel .vox, read and written by hand (ADR-002).
//
// Layout: "VOX " magic, int32 version, then a MAIN chunk whose children carry SIZE, XYZI and RGBA.
// Every chunk is id[4], int32 contentBytes, int32 childrenBytes, content, children. Integers are
// little-endian. Unknown chunks (PACK, nTRN, nGRP, nSHP, MATL, LAYR, rOBJ, ...) are skipped.
//
// THE PALETTE INDEXING CONVENTION. A voxel's colour index c is 1..255. The RGBA chunk holds 256
// entries, and voxel colour c is entry c-1 of the chunk: chunk[0] is the colour of index 1, and the
// chunk's last entry is never addressed by a voxel. VoxPalette is indexed by c directly, so the
// shift happens in exactly two functions below and nowhere else.
inline constexpr std::int32_t VOX_VERSION = 150;
inline constexpr std::size_t VOX_RGBA_CHUNK_BYTES = std::size_t{256} * 4;

enum class VoxError : std::uint8_t
{
  None,
  BadMagic,
  UnsupportedVersion,
  TruncatedChunk,
  MissingMainChunk,
  MissingSize,
  MissingVoxels,
  InvalidDimensions,
  VoxelOutOfBounds,
  FileIo
};

[[nodiscard]] const char* VoxErrorName(VoxError _error) noexcept;

struct VoxDocument
{
  VoxModel model;
  VoxPalette palette{};
  bool hasPalette = false;

  /// The raw 1024-byte RGBA chunk exactly as it appears in the file, for byte-level comparison.
  std::vector<std::uint8_t> rgbaChunk;
};

/// Voxel-indexed palette from a file-order RGBA chunk: palette[c] = chunk[c - 1], palette[0] empty.
[[nodiscard]] VoxPalette PaletteFromRgbaChunk(std::span<const std::uint8_t> _chunk) noexcept;

/// File-order RGBA chunk from a voxel-indexed palette: chunk[i] = palette[i + 1], last entry zero.
[[nodiscard]] std::vector<std::uint8_t> PaletteToRgbaChunk(const VoxPalette& _palette);

[[nodiscard]] VoxError ReadVox(std::span<const std::uint8_t> _bytes, VoxDocument& _outDocument);

/// Serializes one model and its palette. Voxels are written in z, y, x order, so the same model
/// always produces the same bytes.
[[nodiscard]] std::vector<std::uint8_t> WriteVox(const VoxModel& _model, const VoxPalette& _palette);

[[nodiscard]] VoxError LoadVoxFile(const std::filesystem::path& _path, VoxDocument& _outDocument);
[[nodiscard]] bool SaveVoxFile(const std::filesystem::path& _path, const VoxModel& _model, const VoxPalette& _palette);

} // namespace Neuron
