#include "VoxelWireframe.h"

#include <algorithm>
#include <cstdlib>
#include <tuple>

namespace Neuron
{

namespace
{

// A unit lattice edge: its lower endpoint and the axis it runs along (0 = x, 1 = y, 2 = z).
struct EdgeKey
{
  int axis;
  int x;
  int y;
  int z;

  friend auto operator<=>(const EdgeKey&, const EdgeKey&) = default;
};

struct EdgeUse
{
  EdgeKey key;
  FaceDirection direction;
};

[[nodiscard]] int Coordinate(const Int3& _p, int _axis) noexcept
{
  return _axis == 0 ? _p.x : (_axis == 1 ? _p.y : _p.z);
}

[[nodiscard]] int NormalAxis(FaceDirection _direction) noexcept
{
  return static_cast<int>(_direction) / 2;
}

} // namespace

std::vector<LatticeSegment> BuildVoxelWireframe(const VoxelSurface& _surface, int _gridStep)
{
  std::vector<EdgeUse> uses;
  uses.reserve(_surface.faces.size() * 4);
  for (const VoxelFace& face : _surface.faces)
  {
    for (std::size_t i = 0; i < 4; ++i)
    {
      const Int3& p = face.corners[i];
      const Int3& q = face.corners[(i + 1) % 4];
      const Int3 low{std::min(p.x, q.x), std::min(p.y, q.y), std::min(p.z, q.z)};
      const int axis = p.x != q.x ? 0 : (p.y != q.y ? 1 : 2);
      uses.push_back(EdgeUse{EdgeKey{axis, low.x, low.y, low.z}, face.direction});
    }
  }
  std::ranges::sort(uses,
                    [](const EdgeUse& _a, const EdgeUse& _b) { return std::tie(_a.key, _a.direction) < std::tie(_b.key, _b.direction); });

  // Decide which unit edges are drawn.
  std::vector<EdgeKey> kept;
  for (std::size_t begin = 0; begin < uses.size();)
  {
    std::size_t end = begin + 1;
    bool allSame = true;
    while (end < uses.size() && uses[end].key == uses[begin].key)
    {
      allSame = allSame && uses[end].direction == uses[begin].direction;
      ++end;
    }
    const std::size_t count = end - begin;
    const EdgeKey& key = uses[begin].key;
    bool keep = !allSame || count != 2;
    if (!keep && _gridStep > 0)
    {
      // A flat interior edge: draw it when it lies on a grid line. The edge runs along key.axis;
      // the in-plane axis across it is the one that is neither the edge axis nor the normal.
      const int normalAxis = NormalAxis(uses[begin].direction);
      const int acrossAxis = 3 - normalAxis - key.axis;
      const int across = Coordinate(Int3{key.x, key.y, key.z}, acrossAxis);
      keep = across % _gridStep == 0;
    }
    if (keep)
    {
      kept.push_back(key);
    }
    begin = end;
  }

  // Merge runs of collinear unit edges. Sorting by (axis, the two other coordinates, then the
  // coordinate along the axis) puts each line's edges next to each other in order.
  const auto lineOrder = [](const EdgeKey& _e)
  {
    const Int3 p{_e.x, _e.y, _e.z};
    const int u = (_e.axis + 1) % 3;
    const int v = (_e.axis + 2) % 3;
    return std::make_tuple(_e.axis, Coordinate(p, u), Coordinate(p, v), Coordinate(p, _e.axis));
  };
  std::ranges::sort(kept, [&](const EdgeKey& _a, const EdgeKey& _b) { return lineOrder(_a) < lineOrder(_b); });

  std::vector<LatticeSegment> segments;
  for (std::size_t i = 0; i < kept.size();)
  {
    const EdgeKey& start = kept[i];
    std::size_t j = i + 1;
    while (j < kept.size())
    {
      const auto [axisA, uA, vA, tA] = lineOrder(kept[j - 1]);
      const auto [axisB, uB, vB, tB] = lineOrder(kept[j]);
      if (axisA != axisB || uA != uB || vA != vB || tB != tA + 1)
      {
        break;
      }
      ++j;
    }
    const auto length = static_cast<int>(j - i);
    Int3 a{start.x, start.y, start.z};
    Int3 b = a;
    if (start.axis == 0)
    {
      b.x += length;
    }
    else if (start.axis == 1)
    {
      b.y += length;
    }
    else
    {
      b.z += length;
    }
    segments.push_back(LatticeSegment{a, b});
    i = j;
  }
  return segments;
}

std::vector<LatticeSegment> BuildCoarseWireframe(const VoxModel& _model, int _blockVoxels, int _fillPercent, int _gridStep)
{
  if (_model.IsEmpty() || _blockVoxels < 1)
  {
    return {};
  }
  const auto sizeX = static_cast<int>(_model.SizeX());
  const auto sizeY = static_cast<int>(_model.SizeY());
  const auto sizeZ = static_cast<int>(_model.SizeZ());
  const int coarseX = (sizeX + _blockVoxels - 1) / _blockVoxels;
  const int coarseY = (sizeY + _blockVoxels - 1) / _blockVoxels;
  const int coarseZ = (sizeZ + _blockVoxels - 1) / _blockVoxels;
  VoxModel coarse(static_cast<std::uint32_t>(coarseX), static_cast<std::uint32_t>(coarseY), static_cast<std::uint32_t>(coarseZ));
  for (int bz = 0; bz < coarseZ; ++bz)
  {
    for (int by = 0; by < coarseY; ++by)
    {
      for (int bx = 0; bx < coarseX; ++bx)
      {
        int solid = 0;
        int cells = 0;
        for (int z = bz * _blockVoxels; z < std::min((bz + 1) * _blockVoxels, sizeZ); ++z)
        {
          for (int y = by * _blockVoxels; y < std::min((by + 1) * _blockVoxels, sizeY); ++y)
          {
            for (int x = bx * _blockVoxels; x < std::min((bx + 1) * _blockVoxels, sizeX); ++x)
            {
              ++cells;
              solid += _model.At(x, y, z) != 0 ? 1 : 0;
            }
          }
        }
        if (solid > 0 && solid * 100 >= cells * _fillPercent)
        {
          coarse.Set(bx, by, bz, 1);
        }
      }
    }
  }

  std::vector<LatticeSegment> segments = BuildVoxelWireframe(ExtractSurface(coarse), _gridStep);
  const auto scale = [&](const Int3& _p) noexcept
  { return Int3{std::min(_p.x * _blockVoxels, sizeX), std::min(_p.y * _blockVoxels, sizeY), std::min(_p.z * _blockVoxels, sizeZ)}; };
  for (LatticeSegment& segment : segments)
  {
    segment.a = scale(segment.a);
    segment.b = scale(segment.b);
  }
  return segments;
}

SideProfile BuildSideProfile(const VoxelSurface& _surface, int _detailDepthVoxels)
{
  SideProfile profile;
  if (_surface.voxels.empty())
  {
    return profile;
  }
  const int minY = _surface.boundsMin.y;
  const int minZ = _surface.boundsMin.z;
  const int spanY = _surface.boundsMax.y - minY + 1;
  const int spanZ = _surface.boundsMax.z - minZ + 1;
  const int planeX = (_surface.boundsMin.x + _surface.boundsMax.x + 1) / 2;

  // The nearest surface seen from +X, per projected cell; -1 where nothing projects.
  std::vector<int> depth(static_cast<std::size_t>(spanY) * static_cast<std::size_t>(spanZ), -1);
  const auto cell = [&](int _y, int _z) noexcept -> int&
  { return depth[static_cast<std::size_t>(_z - minZ) * static_cast<std::size_t>(spanY) + static_cast<std::size_t>(_y - minY)]; };
  for (const SurfaceVoxel& voxel : _surface.voxels)
  {
    int& nearest = cell(voxel.y, voxel.z);
    nearest = std::max(nearest, static_cast<int>(voxel.x));
  }
  const auto at = [&](int _y, int _z) noexcept
  { return _y < minY || _y >= minY + spanY || _z < minZ || _z >= minZ + spanZ ? -1 : cell(_y, _z); };

  // Each cell contributes its +Y and +Z sides, and the low boundary is covered from outside.
  for (int z = minZ - 1; z < minZ + spanZ; ++z)
  {
    for (int y = minY - 1; y < minY + spanY; ++y)
    {
      const int here = at(y, z);
      const int up = at(y + 1, z);
      const int forward = at(y, z + 1);
      const auto classify = [&](int _other, const LatticeSegment& _segment)
      {
        if ((here < 0) != (_other < 0))
        {
          profile.outline.push_back(_segment);
        }
        else if (here >= 0 && std::abs(here - _other) >= _detailDepthVoxels)
        {
          profile.detail.push_back(_segment);
        }
      };
      classify(up, LatticeSegment{Int3{planeX, y + 1, z}, Int3{planeX, y + 1, z + 1}});
      classify(forward, LatticeSegment{Int3{planeX, y, z + 1}, Int3{planeX, y + 1, z + 1}});
    }
  }
  return profile;
}

} // namespace Neuron
