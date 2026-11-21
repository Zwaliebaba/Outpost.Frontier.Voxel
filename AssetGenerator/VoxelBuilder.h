#pragma once

#include "Random.h"
#include "VoxModel.h"
#include "VoxelMesher.h"

#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace AssetGen
{

using Neuron::Int3;

/// A colour family and the range of its shades a surface may use.
struct Shade
{
  std::uint8_t family; // first palette index of the family
  int low;             // shade offsets within the family, inclusive
  int high;
};

/// Shape primitives for authoring a model in VOX coordinates (+Z up, ships face +Y). Boxes are
/// inclusive on both ends. Writes outside the grid are dropped, so shapes may overhang. Every
/// random choice comes from a seeded generator or a positional hash, so a model is a pure function
/// of its builder code.
class VoxelBuilder
{
public:
  VoxelBuilder(int _sizeX, int _sizeY, int _sizeZ, std::uint32_t _seed);

  [[nodiscard]] Neuron::VoxModel& Model() noexcept
  {
    return m_model;
  }
  [[nodiscard]] const Neuron::VoxModel& Model() const noexcept
  {
    return m_model;
  }
  [[nodiscard]] int SizeX() const noexcept
  {
    return static_cast<int>(m_model.SizeX());
  }
  [[nodiscard]] int SizeY() const noexcept
  {
    return static_cast<int>(m_model.SizeY());
  }
  [[nodiscard]] int SizeZ() const noexcept
  {
    return static_cast<int>(m_model.SizeZ());
  }
  [[nodiscard]] std::uint32_t Seed() const noexcept
  {
    return m_seed;
  }
  [[nodiscard]] Neuron::Pcg32& Rng() noexcept
  {
    return m_rng;
  }

  [[nodiscard]] std::uint8_t At(int _x, int _y, int _z) const noexcept
  {
    return m_model.At(_x, _y, _z);
  }
  [[nodiscard]] bool Solid(int _x, int _y, int _z) const noexcept
  {
    return At(_x, _y, _z) != Neuron::VOX_EMPTY;
  }
  void Set(int _x, int _y, int _z, std::uint8_t _color) noexcept
  {
    m_model.Set(_x, _y, _z, _color);
  }

  void Box(Int3 _min, Int3 _max, std::uint8_t _color) noexcept;
  void Clear(Int3 _min, Int3 _max) noexcept;

  /// A box whose hollow interior is cleared, leaving walls _thickness thick.
  void Shell(Int3 _min, Int3 _max, int _thickness, std::uint8_t _color) noexcept;

  /// Cylinders along an axis: centre in the other two axes, radius in voxels, and an inclusive span.
  void CylinderX(float _centerY, float _centerZ, float _radius, int _x0, int _x1, std::uint8_t _color) noexcept;
  void CylinderY(float _centerX, float _centerZ, float _radius, int _y0, int _y1, std::uint8_t _color) noexcept;
  void CylinderZ(float _centerX, float _centerY, float _radius, int _z0, int _z1, std::uint8_t _color) noexcept;
  void Ellipsoid(float _cx, float _cy, float _cz, float _rx, float _ry, float _rz, std::uint8_t _color) noexcept;

  /// Recolours solid voxels in a box; empty cells stay empty.
  void Paint(Int3 _min, Int3 _max, std::uint8_t _color) noexcept;

  /// Recolours solid voxels in a box with panel-sized blocks of shades, so plating reads as plates.
  void PaintPanels(Int3 _min, Int3 _max, Shade _shade, int _panelSize) noexcept;

  /// Recolours voxels of one exact colour.
  void Replace(Int3 _min, Int3 _max, std::uint8_t _from, std::uint8_t _to) noexcept;

  /// Alternating diagonal hazard stripes on solid voxels in a box.
  void HazardStripes(Int3 _min, Int3 _max, int _period) noexcept;

  /// Copies the half with x < SizeX()/2 onto the other half, mirrored. Makes a hull symmetric.
  void MirrorX() noexcept;

  /// A lattice girder between _min and _max along _axis (0 x, 1 y, 2 z): four longerons on the
  /// box's long edges, and cross-braces every _bay voxels.
  void Truss(Int3 _min, Int3 _max, int _axis, int _bay, std::uint8_t _longeron, std::uint8_t _brace) noexcept;

  /// Raised text in a 4x7 voxel font, drawn on a plane of constant y facing -Y, scaled by _scale.
  /// Supported characters: 0-9, A-Z (a subset), '-', space.
  void TextFrontY(std::string_view _text, int _x, int _y, int _z, int _scale, std::uint8_t _color) noexcept;

  /// A hexagonal outline emblem on a plane of constant x or y.
  void HexEmblemY(int _cx, int _y, int _cz, int _radius, std::uint8_t _plate, std::uint8_t _mark) noexcept;
  void HexEmblemX(int _x, int _cy, int _cz, int _radius, std::uint8_t _plate, std::uint8_t _mark) noexcept;

private:
  Neuron::VoxModel m_model;
  std::uint32_t m_seed;
  Neuron::Pcg32 m_rng;
};

} // namespace AssetGen
