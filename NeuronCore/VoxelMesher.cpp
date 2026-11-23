#include "VoxelMesher.h"

#include <algorithm>

namespace Neuron
{

namespace
{

constexpr std::array<Int3, FACE_DIRECTION_COUNT> NORMALS = {Int3{1, 0, 0},  Int3{-1, 0, 0}, Int3{0, 1, 0},
                                                            Int3{0, -1, 0}, Int3{0, 0, 1},  Int3{0, 0, -1}};

// Corner offsets of each face relative to the voxel's minimum corner, counter-clockwise seen from
// outside (right-hand rule about the outward normal).
constexpr std::array<std::array<Int3, 4>, FACE_DIRECTION_COUNT> CORNERS = {{
  {Int3{1, 0, 0}, Int3{1, 1, 0}, Int3{1, 1, 1}, Int3{1, 0, 1}}, // +X
  {Int3{0, 0, 0}, Int3{0, 0, 1}, Int3{0, 1, 1}, Int3{0, 1, 0}}, // -X
  {Int3{0, 1, 0}, Int3{0, 1, 1}, Int3{1, 1, 1}, Int3{1, 1, 0}}, // +Y
  {Int3{0, 0, 0}, Int3{1, 0, 0}, Int3{1, 0, 1}, Int3{0, 0, 1}}, // -Y
  {Int3{0, 0, 1}, Int3{1, 0, 1}, Int3{1, 1, 1}, Int3{0, 1, 1}}, // +Z
  {Int3{0, 0, 0}, Int3{0, 1, 0}, Int3{1, 1, 0}, Int3{1, 0, 0}}, // -Z
}};

[[nodiscard]] bool Solid(const VoxModel& _model, int _x, int _y, int _z, const VoxPalette* _palette = nullptr) noexcept
{
  const std::uint8_t index = _model.At(_x, _y, _z);
  return index != VOX_EMPTY && (_palette == nullptr || (*_palette)[index].a == 255);
}

// How recessed a face is: the count of solid cells among the eight that ring the cell in front of
// the face, quantized to two bits.
[[nodiscard]] std::uint16_t FaceOcclusion(const VoxModel& _model, int _x, int _y, int _z, FaceDirection _direction,
                                          const VoxPalette* _palette) noexcept
{
  const Int3 n = NORMALS[static_cast<std::size_t>(_direction)];
  const int fx = _x + n.x;
  const int fy = _y + n.y;
  const int fz = _z + n.z;
  int count = 0;
  for (int a = -1; a <= 1; ++a)
  {
    for (int b = -1; b <= 1; ++b)
    {
      if (a == 0 && b == 0)
      {
        continue;
      }
      int dx = 0;
      int dy = 0;
      int dz = 0;
      if (n.x != 0)
      {
        dy = a;
        dz = b;
      }
      else if (n.y != 0)
      {
        dx = a;
        dz = b;
      }
      else
      {
        dx = a;
        dy = b;
      }
      count += Solid(_model, fx + dx, fy + dy, fz + dz, _palette) ? 1 : 0;
    }
  }
  if (count == 0)
  {
    return 0;
  }
  if (count <= 2)
  {
    return 1;
  }
  return count <= 4 ? std::uint16_t{2} : std::uint16_t{3};
}

// The surface of the cells in [_min, _max], neighbours read from the whole model. Faces only when asked.
[[nodiscard]] VoxelSurface Extract(const VoxModel& _model, const VoxPalette* _palette, const Int3& _min, const Int3& _max, bool _withFaces)
{
  VoxelSurface surface;
  surface.boundsMin = Int3{static_cast<int>(_model.SizeX()), static_cast<int>(_model.SizeY()), static_cast<int>(_model.SizeZ())};
  for (int z = _min.z; z <= _max.z; ++z)
  {
    for (int y = _min.y; y <= _max.y; ++y)
    {
      for (int x = _min.x; x <= _max.x; ++x)
      {
        const std::uint8_t colorIndex = _model.At(x, y, z);
        if (colorIndex == VOX_EMPTY)
        {
          continue;
        }
        ++surface.solidCount;
        surface.boundsMin = Int3{std::min(surface.boundsMin.x, x), std::min(surface.boundsMin.y, y), std::min(surface.boundsMin.z, z)};
        surface.boundsMax = Int3{std::max(surface.boundsMax.x, x), std::max(surface.boundsMax.y, y), std::max(surface.boundsMax.z, z)};

        SurfaceVoxel voxel{static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(z), colorIndex, 0, 0, 0};
        for (std::uint32_t face = 0; face < FACE_DIRECTION_COUNT; ++face)
        {
          const Int3 n = NORMALS[face];
          const std::uint8_t neighbor = _model.At(x + n.x, y + n.y, z + n.z);
          if (neighbor == colorIndex || Solid(_model, x + n.x, y + n.y, z + n.z, _palette))
          {
            continue; // shared with a solid neighbour: never visible
          }
          const auto direction = static_cast<FaceDirection>(face);
          voxel.exposedFaceMask = static_cast<std::uint8_t>(voxel.exposedFaceMask | (1u << face));
          voxel.faceOcclusion =
            static_cast<std::uint16_t>(voxel.faceOcclusion | (FaceOcclusion(_model, x, y, z, direction, _palette) << (face * 2)));

          if (!_withFaces)
          {
            continue;
          }
          VoxelFace out{Int3{x, y, z}, direction, colorIndex, {}};
          for (std::size_t corner = 0; corner < 4; ++corner)
          {
            const Int3 offset = CORNERS[face][corner];
            out.corners[corner] = Int3{x + offset.x, y + offset.y, z + offset.z};
          }
          surface.faces.push_back(out);
        }
        if (voxel.exposedFaceMask != 0)
        {
          surface.voxels.push_back(voxel);
        }
      }
    }
  }
  if (surface.solidCount == 0)
  {
    surface.boundsMin = Int3{0, 0, 0};
  }
  return surface;
}

} // namespace

Int3 FaceNormal(FaceDirection _direction) noexcept
{
  return NORMALS[static_cast<std::size_t>(_direction)];
}

std::vector<VoxelFace> MeshFaces(const VoxModel& _model)
{
  return ExtractSurface(_model).faces;
}

VoxelSurface ExtractSurface(const VoxModel& _model, const VoxPalette* _palette)
{
  const Int3 max{static_cast<int>(_model.SizeX()) - 1, static_cast<int>(_model.SizeY()) - 1, static_cast<int>(_model.SizeZ()) - 1};
  return Extract(_model, _palette, Int3{0, 0, 0}, max, true);
}

VoxelSurface ExtractSurfaceRegion(const VoxModel& _model, const VoxPalette* _palette, const Int3& _min, const Int3& _max)
{
  const Int3 low{std::max(_min.x, 0), std::max(_min.y, 0), std::max(_min.z, 0)};
  const Int3 high{std::min(_max.x, static_cast<int>(_model.SizeX()) - 1), std::min(_max.y, static_cast<int>(_model.SizeY()) - 1),
                  std::min(_max.z, static_cast<int>(_model.SizeZ()) - 1)};
  return Extract(_model, _palette, low, high, false);
}

} // namespace Neuron
