#include "MaskedSurfaces.h"

#include "Palette.h"

#include <algorithm>
#include <utility>

namespace Outpost
{

namespace
{

constexpr int BRICK = static_cast<int>(Neuron::MASK_BRICK_CELLS);

[[nodiscard]] int Bricks(std::uint32_t _cells) noexcept
{
  return (static_cast<int>(_cells) + BRICK - 1) / BRICK;
}

} // namespace

void MaskedSurfaces::Load(std::shared_ptr<const CombatContent> _content, std::vector<const Neuron::VoxModel*> _sceneModels)
{
  m_content = std::move(_content);
  m_sceneModels = std::move(_sceneModels);
  m_bases.assign(m_content != nullptr ? m_content->archetypes.size() : 0, std::nullopt);
  m_sceneModels.resize(m_bases.size(), nullptr);
  m_entries.clear();
}

Neuron::VoxModel MaskedSurfaces::BaseAppearance(const VoxelArchetype& _archetype, const Neuron::VoxModel* _sceneModel)
{
  Neuron::VoxModel base = _archetype.voxels;
  if (_sceneModel == nullptr || _sceneModel->SizeX() != base.SizeX() || _sceneModel->SizeY() != base.SizeY() ||
      _sceneModel->SizeZ() != base.SizeZ())
  {
    return base;
  }
  for (int z = 0; z < static_cast<int>(base.SizeZ()); ++z)
  {
    for (int y = 0; y < static_cast<int>(base.SizeY()); ++y)
    {
      for (int x = 0; x < static_cast<int>(base.SizeX()); ++x)
      {
        const std::uint8_t shown = _sceneModel->At(x, y, z);
        if (shown != Neuron::VOX_EMPTY && base.At(x, y, z) != Neuron::VOX_EMPTY)
        {
          base.Set(x, y, z, shown);
        }
      }
    }
  }
  return base;
}

const Neuron::VoxModel& MaskedSurfaces::Base(std::size_t _archetype)
{
  if (!m_bases[_archetype].has_value())
  {
    m_bases[_archetype] = BaseAppearance(m_content->archetypes[_archetype], m_sceneModels[_archetype]);
  }
  return *m_bases[_archetype];
}

std::size_t MaskedSurfaces::Update(ClientCombat& _combat, std::span<const ObjectId> _order, std::size_t _budget)
{
  if (m_content == nullptr)
  {
    return 0;
  }
  std::erase_if(m_entries, [&](const Entry& _entry) { return _combat.Find(_entry.drawn.object) == nullptr; });
  for (ClientMask& mask : _combat.Masks())
  {
    auto found = std::ranges::find(m_entries, mask.object, [](const Entry& _entry) { return _entry.drawn.object; });
    if (found == m_entries.end())
    {
      Entry entry;
      entry.archetype = mask.archetype;
      entry.drawn.object = mask.object;
      const Neuron::Int3& size = mask.mask.Size();
      entry.appearance =
        Neuron::VoxModel(static_cast<std::uint32_t>(size.x), static_cast<std::uint32_t>(size.y), static_cast<std::uint32_t>(size.z));
      entry.brickGrid = Neuron::Int3{Bricks(entry.appearance.SizeX()), Bricks(entry.appearance.SizeY()), Bricks(entry.appearance.SizeZ())};
      const auto count = static_cast<std::size_t>(entry.brickGrid.x) * static_cast<std::size_t>(entry.brickGrid.y) *
                         static_cast<std::size_t>(entry.brickGrid.z);
      entry.bricks.assign(count, Neuron::VoxelSurface{});
      entry.waiting.assign(count, 0);
      mask.whole = true; // a new surface is built from the whole mask
      m_entries.push_back(std::move(entry));
      found = m_entries.end() - 1;
    }
    if (found->revision != mask.revision || mask.whole || !mask.changed.empty())
    {
      Take(*found, mask);
    }
  }

  std::size_t spent = 0;
  for (const ObjectId object : _order)
  {
    const auto found = std::ranges::find(m_entries, object, [](const Entry& _entry) { return _entry.drawn.object; });
    if (found != m_entries.end() && spent < _budget)
    {
      spent += Extract(*found, _budget - spent);
    }
  }
  for (Entry& entry : m_entries)
  {
    if (spent < _budget)
    {
      spent += Extract(entry, _budget - spent);
    }
  }
  for (Entry& entry : m_entries)
  {
    if (entry.rebuild && entry.waitingCount == 0)
    {
      Rebuild(entry);
    }
  }
  return spent;
}

void MaskedSurfaces::Take(Entry& _entry, ClientMask& _mask)
{
  const Neuron::VoxModel& base = Base(_entry.archetype);
  if (_mask.whole)
  {
    for (int z = 0; z < static_cast<int>(base.SizeZ()); ++z)
    {
      for (int y = 0; y < static_cast<int>(base.SizeY()); ++y)
      {
        for (int x = 0; x < static_cast<int>(base.SizeX()); ++x)
        {
          _entry.appearance.Set(x, y, z, _mask.mask.Present(x, y, z) ? base.At(x, y, z) : Neuron::VOX_EMPTY);
        }
      }
    }
    std::ranges::fill(_entry.waiting, std::uint8_t{1});
    _entry.waitingCount = _entry.waiting.size();
  }
  else
  {
    for (const std::uint32_t cell : _mask.changed)
    {
      const Neuron::Int3 at = _mask.mask.CellOf(cell);
      _entry.appearance.Set(at.x, at.y, at.z, _mask.mask.PresentAt(cell) ? base.At(at.x, at.y, at.z) : Neuron::VOX_EMPTY);
      Mark(_entry, at);
    }
  }
  _mask.whole = false;
  _mask.changed.clear();
  _entry.revision = _mask.revision;
  _entry.rebuild = true;
}

void MaskedSurfaces::Mark(Entry& _entry, const Neuron::Int3& _cell)
{
  // A cell is read by the faces of its six neighbours and by the occlusion of the cells around it,
  // so every brick within one cell of it is extracted again.
  for (int dz = -1; dz <= 1; ++dz)
  {
    for (int dy = -1; dy <= 1; ++dy)
    {
      for (int dx = -1; dx <= 1; ++dx)
      {
        const int x = _cell.x + dx;
        const int y = _cell.y + dy;
        const int z = _cell.z + dz;
        if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(_entry.appearance.SizeX()) ||
            y >= static_cast<int>(_entry.appearance.SizeY()) || z >= static_cast<int>(_entry.appearance.SizeZ()))
        {
          continue;
        }
        const auto gridX = static_cast<std::size_t>(_entry.brickGrid.x);
        const auto gridY = static_cast<std::size_t>(_entry.brickGrid.y);
        const std::size_t brick =
          static_cast<std::size_t>(x / BRICK) + gridX * (static_cast<std::size_t>(y / BRICK) + gridY * static_cast<std::size_t>(z / BRICK));
        if (_entry.waiting[brick] == 0)
        {
          _entry.waiting[brick] = 1;
          ++_entry.waitingCount;
        }
      }
    }
  }
}

std::size_t MaskedSurfaces::Extract(Entry& _entry, std::size_t _budget)
{
  std::size_t spent = 0;
  const Neuron::VoxPalette& palette = CanonicalPalette();
  for (std::size_t brick = 0; brick < _entry.waiting.size() && spent < _budget && _entry.waitingCount > 0; ++brick)
  {
    if (_entry.waiting[brick] == 0)
    {
      continue;
    }
    const int bx = static_cast<int>(brick % static_cast<std::size_t>(_entry.brickGrid.x));
    const int by = static_cast<int>(brick / static_cast<std::size_t>(_entry.brickGrid.x) % static_cast<std::size_t>(_entry.brickGrid.y));
    const int bz = static_cast<int>(brick / (static_cast<std::size_t>(_entry.brickGrid.x) * static_cast<std::size_t>(_entry.brickGrid.y)));
    const Neuron::Int3 low{bx * BRICK, by * BRICK, bz * BRICK};
    const Neuron::Int3 high{low.x + BRICK - 1, low.y + BRICK - 1, low.z + BRICK - 1};
    Neuron::VoxelSurface extracted = Neuron::ExtractSurfaceRegion(_entry.appearance, &palette, low, high);
    spent += extracted.solidCount > 0 || _entry.bricks[brick].solidCount > 0 ? 1 : 0;
    _entry.bricks[brick] = std::move(extracted);
    _entry.waiting[brick] = 0;
    --_entry.waitingCount;
  }
  return spent;
}

void MaskedSurfaces::Rebuild(Entry& _entry)
{
  Neuron::VoxelSurface& surface = _entry.drawn.surface;
  surface = Neuron::VoxelSurface{};
  surface.boundsMin = Neuron::Int3{static_cast<int>(_entry.appearance.SizeX()), static_cast<int>(_entry.appearance.SizeY()),
                                   static_cast<int>(_entry.appearance.SizeZ())};
  for (const Neuron::VoxelSurface& brick : _entry.bricks)
  {
    if (brick.solidCount == 0)
    {
      continue;
    }
    surface.voxels.insert(surface.voxels.end(), brick.voxels.begin(), brick.voxels.end());
    surface.solidCount += brick.solidCount;
    surface.boundsMin = Neuron::Int3{std::min(surface.boundsMin.x, brick.boundsMin.x), std::min(surface.boundsMin.y, brick.boundsMin.y),
                                     std::min(surface.boundsMin.z, brick.boundsMin.z)};
    surface.boundsMax = Neuron::Int3{std::max(surface.boundsMax.x, brick.boundsMax.x), std::max(surface.boundsMax.y, brick.boundsMax.y),
                                     std::max(surface.boundsMax.z, brick.boundsMax.z)};
  }
  if (surface.solidCount == 0)
  {
    surface.boundsMin = Neuron::Int3{0, 0, 0};
  }
  ++_entry.drawn.generation;
  _entry.drawn.ready = true;
  _entry.rebuild = false;
}

const MaskedSurface* MaskedSurfaces::Find(ObjectId _object) const noexcept
{
  const auto found = std::ranges::find(m_entries, _object, [](const Entry& _entry) { return _entry.drawn.object; });
  return found != m_entries.end() ? &found->drawn : nullptr;
}

std::size_t MaskedSurfaces::WaitingBricks() const noexcept
{
  std::size_t waiting = 0;
  for (const Entry& entry : m_entries)
  {
    waiting += entry.waitingCount;
  }
  return waiting;
}

} // namespace Outpost
