#include "VoxelCarve.h"

#include <algorithm>
#include <cstdlib>

namespace Neuron
{

namespace
{

constexpr std::array<Int3, 6> FACE_NEIGHBORS{Int3{1, 0, 0}, Int3{-1, 0, 0}, Int3{0, 1, 0}, Int3{0, -1, 0}, Int3{0, 0, 1}, Int3{0, 0, -1}};

/// A candidate and the key that orders it.
struct Candidate
{
  std::uint32_t center = 0;
  std::uint32_t distanceSquared = 0;
  std::uint32_t index = 0;
};

/// The sphere centers of a shape: the impact cell, and for a capsule the cells an integer line walk
/// visits along the direction, lengthCells in all.
[[nodiscard]] std::vector<Int3> Centers(const CarveSpec& _spec)
{
  std::vector<Int3> centers{_spec.cell};
  if (_spec.shape != CarveShape::Capsule || _spec.lengthCells <= 1)
  {
    return centers;
  }
  const int ax = std::abs(_spec.direction.x);
  const int ay = std::abs(_spec.direction.y);
  const int az = std::abs(_spec.direction.z);
  const int steps = std::max({ax, ay, az});
  if (steps == 0)
  {
    return centers;
  }
  // Bresenham in three dimensions: the dominant axis advances every step, the others when their
  // accumulated error passes half a step.
  const int sx = _spec.direction.x < 0 ? -1 : 1;
  const int sy = _spec.direction.y < 0 ? -1 : 1;
  const int sz = _spec.direction.z < 0 ? -1 : 1;
  int ex = 0;
  int ey = 0;
  int ez = 0;
  Int3 at = _spec.cell;
  while (centers.size() < _spec.lengthCells)
  {
    ex += ax;
    ey += ay;
    ez += az;
    if (2 * ex >= steps)
    {
      at.x += sx;
      ex -= steps;
    }
    if (2 * ey >= steps)
    {
      at.y += sy;
      ey -= steps;
    }
    if (2 * ez >= steps)
    {
      at.z += sz;
      ez -= steps;
    }
    centers.push_back(at);
  }
  return centers;
}

[[nodiscard]] bool Exposed(const VoxelMask& _mask, const Int3& _cell) noexcept
{
  return std::ranges::any_of(FACE_NEIGHBORS,
                             [&](const Int3& _step) { return !_mask.Present(_cell.x + _step.x, _cell.y + _step.y, _cell.z + _step.z); });
}

} // namespace

std::uint32_t HashCell(std::uint32_t _seed, std::uint32_t _value) noexcept
{
  std::uint32_t h = _seed ^ 0x9E3779B9u;
  h ^= _value * 0x85EBCA6Bu;
  h = (h << 13) | (h >> 19);
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return h;
}

CarveResult Carve(VoxelMask& _mask, const VoxModel& _archetype, const StrengthTable& _strength, const CarveSpec& _spec,
                  const VoxelMask* _toughened, std::uint32_t _toughFactor)
{
  CarveResult result;
  _mask.SetLastEvent(_spec.sequence);
  const int radius = _spec.radiusCells;
  const int radiusSquared = radius * radius;
  const std::vector<Int3> centers = Centers(_spec);

  std::vector<Candidate> candidates;
  for (std::uint32_t c = 0; c < centers.size(); ++c)
  {
    const Int3& center = centers[c];
    for (int dz = -radius; dz <= radius; ++dz)
    {
      for (int dy = -radius; dy <= radius; ++dy)
      {
        for (int dx = -radius; dx <= radius; ++dx)
        {
          const int distanceSquared = dx * dx + dy * dy + dz * dz;
          const Int3 cell{center.x + dx, center.y + dy, center.z + dz};
          if (distanceSquared <= radiusSquared && _mask.Present(cell.x, cell.y, cell.z))
          {
            candidates.push_back(Candidate{c, static_cast<std::uint32_t>(distanceSquared), _mask.Index(cell.x, cell.y, cell.z)});
          }
        }
      }
    }
  }
  // A cell inside several spheres keeps its first key: sort by cell, keep the least key, then order.
  const auto key = [](const Candidate& _a, const Candidate& _b)
  {
    if (_a.center != _b.center)
    {
      return _a.center < _b.center;
    }
    if (_a.distanceSquared != _b.distanceSquared)
    {
      return _a.distanceSquared < _b.distanceSquared;
    }
    return _a.index < _b.index;
  };
  std::ranges::sort(candidates,
                    [&](const Candidate& _a, const Candidate& _b) { return _a.index != _b.index ? _a.index < _b.index : key(_a, _b); });
  const auto duplicates = std::ranges::unique(candidates, {}, &Candidate::index);
  candidates.erase(duplicates.begin(), duplicates.end());
  std::ranges::sort(candidates, key);

  const std::uint32_t impact =
    _mask.Contains(_spec.cell.x, _spec.cell.y, _spec.cell.z) ? _mask.Index(_spec.cell.x, _spec.cell.y, _spec.cell.z) : 0xFFFFFFFFu;
  std::uint32_t energyLeft = _spec.energy;
  for (const Candidate& candidate : candidates)
  {
    const Int3 cell = _mask.CellOf(candidate.index);
    std::uint32_t strength = _strength[_archetype.At(cell.x, cell.y, cell.z)];
    if (strength != UNBREAKABLE && _toughened != nullptr && _toughened->PresentAt(candidate.index))
    {
      const std::uint64_t tough = static_cast<std::uint64_t>(strength) * _toughFactor;
      strength = tough >= UNBREAKABLE ? UNBREAKABLE - 1 : static_cast<std::uint32_t>(tough);
    }
    if (strength == UNBREAKABLE || (candidate.index != impact && !Exposed(_mask, cell)))
    {
      continue;
    }
    const std::uint32_t cost = strength + HashCell(_spec.seed, candidate.index) % (strength / 2 + 1);
    if (cost > energyLeft)
    {
      continue;
    }
    energyLeft -= cost;
    _mask.SetAt(candidate.index, false);
    result.removed.push_back(candidate.index);
  }
  result.energySpent = _spec.energy - energyLeft;
  return result;
}

std::vector<std::uint32_t> Patch(VoxelMask& _mask, const VoxelMask& _allowed, const PatchSpec& _spec)
{
  std::vector<std::uint32_t> restored;
  _mask.SetLastEvent(_spec.sequence);
  if (!(_mask.Size() == _allowed.Size()))
  {
    return restored;
  }
  for (std::uint32_t i = 0; i < _mask.CellCount() && restored.size() < _spec.maxCells; ++i)
  {
    if (_mask.PresentAt(i) || !_allowed.PresentAt(i))
    {
      continue;
    }
    const Int3 cell = _mask.CellOf(i);
    const bool touches = std::ranges::any_of(FACE_NEIGHBORS, [&](const Int3& _step)
                                             { return _mask.Present(cell.x + _step.x, cell.y + _step.y, cell.z + _step.z); });
    if (touches)
    {
      _mask.SetAt(i, true);
      restored.push_back(i);
    }
  }
  return restored;
}

std::vector<std::vector<std::uint32_t>> DetachedParts(const VoxelMask& _mask, std::span<const std::uint32_t> _anchors,
                                                      std::span<const std::uint32_t> /*_removed*/)
{
  std::vector<bool> visited(_mask.CellCount(), false);
  std::vector<std::uint32_t> queue;
  const auto fill = [&](std::uint32_t _start, std::vector<std::uint32_t>* _part)
  {
    queue.clear();
    queue.push_back(_start);
    visited[_start] = true;
    for (std::size_t head = 0; head < queue.size(); ++head)
    {
      const std::uint32_t index = queue[head];
      if (_part != nullptr)
      {
        _part->push_back(index);
      }
      const Int3 cell = _mask.CellOf(index);
      for (const Int3& step : FACE_NEIGHBORS)
      {
        const Int3 next{cell.x + step.x, cell.y + step.y, cell.z + step.z};
        if (_mask.Present(next.x, next.y, next.z))
        {
          const std::uint32_t nextIndex = _mask.Index(next.x, next.y, next.z);
          if (!visited[nextIndex])
          {
            visited[nextIndex] = true;
            queue.push_back(nextIndex);
          }
        }
      }
    }
  };
  for (const std::uint32_t anchor : _anchors)
  {
    if (_mask.PresentAt(anchor) && !visited[anchor])
    {
      fill(anchor, nullptr);
    }
  }
  std::vector<std::vector<std::uint32_t>> parts;
  for (std::uint32_t i = 0; i < _mask.CellCount(); ++i)
  {
    if (_mask.PresentAt(i) && !visited[i])
    {
      std::vector<std::uint32_t>& part = parts.emplace_back();
      fill(i, &part);
      std::ranges::sort(part);
    }
  }
  return parts;
}

} // namespace Neuron
