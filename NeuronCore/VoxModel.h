#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Neuron
{

// MagicaVoxel limits each model to 256 cells on every axis; larger structures are assembled from
// separate models with scene transforms.
inline constexpr std::uint32_t VOX_MAX_DIMENSION = 256;

// Colour index 0 is empty space. Indices 1..255 are colours.
inline constexpr std::uint8_t VOX_EMPTY = 0;

struct Rgba8
{
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t a;

  friend bool operator==(const Rgba8&, const Rgba8&) = default;
};

/// A palette indexed by VOXEL COLOUR INDEX: entry 0 is empty space and is never drawn, entries
/// 1..255 are the colours a voxel can carry. This is NOT the layout of a .vox RGBA chunk, which is
/// shifted by one; VoxFile.h converts at the file boundary and nowhere else.
using VoxPalette = std::array<Rgba8, 256>;

/// A dense grid of colour indices in VOX coordinates: right-handed, +Z up. See ModelAxes.h for the
/// conversion to world space.
class VoxModel
{
public:
  VoxModel() = default;
  VoxModel(std::uint32_t _sizeX, std::uint32_t _sizeY, std::uint32_t _sizeZ);

  [[nodiscard]] std::uint32_t SizeX() const noexcept
  {
    return m_sizeX;
  }
  [[nodiscard]] std::uint32_t SizeY() const noexcept
  {
    return m_sizeY;
  }
  [[nodiscard]] std::uint32_t SizeZ() const noexcept
  {
    return m_sizeZ;
  }
  [[nodiscard]] bool IsEmpty() const noexcept
  {
    return m_cells.empty();
  }

  [[nodiscard]] bool Contains(int _x, int _y, int _z) const noexcept;

  /// The colour index at a cell, or VOX_EMPTY for any coordinate outside the grid.
  [[nodiscard]] std::uint8_t At(int _x, int _y, int _z) const noexcept;

  /// Writes a cell. A coordinate outside the grid is ignored, which lets shape builders overhang.
  void Set(int _x, int _y, int _z, std::uint8_t _colorIndex) noexcept;

  [[nodiscard]] std::size_t CountSolid() const noexcept;

  friend bool operator==(const VoxModel&, const VoxModel&) = default;

private:
  [[nodiscard]] std::size_t Index(int _x, int _y, int _z) const noexcept;

  std::uint32_t m_sizeX = 0;
  std::uint32_t m_sizeY = 0;
  std::uint32_t m_sizeZ = 0;
  std::vector<std::uint8_t> m_cells;
};

} // namespace Neuron
