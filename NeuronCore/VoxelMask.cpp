#include "VoxelMask.h"

namespace Neuron
{

namespace
{

constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ull;
constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

void HashWord(std::uint64_t& _hash, std::uint64_t _word) noexcept
{
  for (int byte = 0; byte < 8; ++byte)
  {
    _hash ^= (_word >> (byte * 8)) & 0xFFu;
    _hash *= FNV_PRIME;
  }
}

[[nodiscard]] int BricksAlong(int _cells) noexcept
{
  return (_cells + MASK_BRICK_CELLS - 1) / MASK_BRICK_CELLS;
}

} // namespace

VoxelMask::VoxelMask(const Int3& _size)
  : m_size(_size)
{
  if (_size.x <= 0 || _size.y <= 0 || _size.z <= 0)
  {
    m_size = Int3{0, 0, 0};
    return;
  }
  m_cellCount = static_cast<std::uint32_t>(_size.x) * static_cast<std::uint32_t>(_size.y) * static_cast<std::uint32_t>(_size.z);
  m_words.assign((static_cast<std::size_t>(m_cellCount) + 63) / 64, 0);
}

VoxelMask VoxelMask::FromModel(const VoxModel& _model)
{
  VoxelMask mask(Int3{static_cast<int>(_model.SizeX()), static_cast<int>(_model.SizeY()), static_cast<int>(_model.SizeZ())});
  for (int z = 0; z < mask.m_size.z; ++z)
  {
    for (int y = 0; y < mask.m_size.y; ++y)
    {
      for (int x = 0; x < mask.m_size.x; ++x)
      {
        if (_model.At(x, y, z) != VOX_EMPTY)
        {
          mask.SetAt(mask.Index(x, y, z), true);
        }
      }
    }
  }
  return mask;
}

bool VoxelMask::Contains(int _x, int _y, int _z) const noexcept
{
  return _x >= 0 && _y >= 0 && _z >= 0 && _x < m_size.x && _y < m_size.y && _z < m_size.z;
}

std::uint32_t VoxelMask::Index(int _x, int _y, int _z) const noexcept
{
  return static_cast<std::uint32_t>(_x) +
         static_cast<std::uint32_t>(m_size.x) *
           (static_cast<std::uint32_t>(_y) + static_cast<std::uint32_t>(m_size.y) * static_cast<std::uint32_t>(_z));
}

Int3 VoxelMask::CellOf(std::uint32_t _index) const noexcept
{
  const auto sizeX = static_cast<std::uint32_t>(m_size.x);
  const auto sizeY = static_cast<std::uint32_t>(m_size.y);
  return Int3{static_cast<int>(_index % sizeX), static_cast<int>((_index / sizeX) % sizeY), static_cast<int>(_index / (sizeX * sizeY))};
}

bool VoxelMask::Present(int _x, int _y, int _z) const noexcept
{
  return Contains(_x, _y, _z) && PresentAt(Index(_x, _y, _z));
}

bool VoxelMask::PresentAt(std::uint32_t _index) const noexcept
{
  return _index < m_cellCount && ((m_words[_index / 64] >> (_index % 64)) & 1u) != 0;
}

void VoxelMask::SetAt(std::uint32_t _index, bool _present) noexcept
{
  if (_index >= m_cellCount || PresentAt(_index) == _present)
  {
    return;
  }
  const std::uint64_t bit = std::uint64_t{1} << (_index % 64);
  if (_present)
  {
    m_words[_index / 64] |= bit;
    ++m_presentCount;
  }
  else
  {
    m_words[_index / 64] &= ~bit;
    --m_presentCount;
  }
}

std::uint64_t VoxelMask::Hash() const noexcept
{
  std::uint64_t hash = FNV_OFFSET;
  HashWord(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(m_size.x)));
  HashWord(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(m_size.y)));
  HashWord(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(m_size.z)));
  for (const std::uint64_t word : m_words)
  {
    HashWord(hash, word);
  }
  return hash;
}

Int3 VoxelMask::BrickGrid() const noexcept
{
  return Int3{BricksAlong(m_size.x), BricksAlong(m_size.y), BricksAlong(m_size.z)};
}

std::uint32_t VoxelMask::BrickCount() const noexcept
{
  const Int3 grid = BrickGrid();
  return static_cast<std::uint32_t>(grid.x) * static_cast<std::uint32_t>(grid.y) * static_cast<std::uint32_t>(grid.z);
}

std::array<std::uint64_t, MASK_BRICK_WORDS> VoxelMask::BrickBits(std::uint32_t _brick) const noexcept
{
  std::array<std::uint64_t, MASK_BRICK_WORDS> bits{};
  const Int3 grid = BrickGrid();
  if (_brick >= BrickCount())
  {
    return bits;
  }
  const int bx = static_cast<int>(_brick % static_cast<std::uint32_t>(grid.x)) * MASK_BRICK_CELLS;
  const int by = static_cast<int>((_brick / static_cast<std::uint32_t>(grid.x)) % static_cast<std::uint32_t>(grid.y)) * MASK_BRICK_CELLS;
  const int bz = static_cast<int>(_brick / static_cast<std::uint32_t>(grid.x * grid.y)) * MASK_BRICK_CELLS;
  for (int lz = 0; lz < MASK_BRICK_CELLS; ++lz)
  {
    for (int ly = 0; ly < MASK_BRICK_CELLS; ++ly)
    {
      for (int lx = 0; lx < MASK_BRICK_CELLS; ++lx)
      {
        if (Present(bx + lx, by + ly, bz + lz))
        {
          const int bit = lx + MASK_BRICK_CELLS * (ly + MASK_BRICK_CELLS * lz);
          bits[static_cast<std::size_t>(bit / 64)] |= std::uint64_t{1} << (bit % 64);
        }
      }
    }
  }
  return bits;
}

void VoxelMask::SetBrickBits(std::uint32_t _brick, const std::array<std::uint64_t, MASK_BRICK_WORDS>& _bits) noexcept
{
  const Int3 grid = BrickGrid();
  if (_brick >= BrickCount())
  {
    return;
  }
  const int bx = static_cast<int>(_brick % static_cast<std::uint32_t>(grid.x)) * MASK_BRICK_CELLS;
  const int by = static_cast<int>((_brick / static_cast<std::uint32_t>(grid.x)) % static_cast<std::uint32_t>(grid.y)) * MASK_BRICK_CELLS;
  const int bz = static_cast<int>(_brick / static_cast<std::uint32_t>(grid.x * grid.y)) * MASK_BRICK_CELLS;
  for (int lz = 0; lz < MASK_BRICK_CELLS; ++lz)
  {
    for (int ly = 0; ly < MASK_BRICK_CELLS; ++ly)
    {
      for (int lx = 0; lx < MASK_BRICK_CELLS; ++lx)
      {
        if (Contains(bx + lx, by + ly, bz + lz))
        {
          const int bit = lx + MASK_BRICK_CELLS * (ly + MASK_BRICK_CELLS * lz);
          SetAt(Index(bx + lx, by + ly, bz + lz), ((_bits[static_cast<std::size_t>(bit / 64)] >> (bit % 64)) & 1u) != 0);
        }
      }
    }
  }
}

MaskSnapshot TakeSnapshot(const VoxelMask& _mask)
{
  return MaskSnapshot{_mask.LastEvent(), _mask.Size(), std::vector<std::uint64_t>(_mask.Words().begin(), _mask.Words().end())};
}

bool RestoreSnapshot(const MaskSnapshot& _snapshot, VoxelMask& _outMask)
{
  VoxelMask mask(_snapshot.size);
  if (mask.Words().size() != _snapshot.words.size())
  {
    return false;
  }
  for (std::uint32_t i = 0; i < mask.CellCount(); ++i)
  {
    if (((_snapshot.words[i / 64] >> (i % 64)) & 1u) != 0)
    {
      mask.SetAt(i, true);
    }
  }
  // Bits past the last cell carry nothing: a snapshot that sets one is malformed.
  const std::uint32_t tail = mask.CellCount() % 64;
  if (tail != 0 && (_snapshot.words.back() >> tail) != 0)
  {
    return false;
  }
  mask.SetLastEvent(_snapshot.lastEvent);
  _outMask = std::move(mask);
  return true;
}

MaskDiff DiffBricks(const VoxelMask& _from, const VoxelMask& _to)
{
  MaskDiff diff;
  diff.lastEvent = _to.LastEvent();
  if (!(_from.Size() == _to.Size()))
  {
    return diff;
  }
  for (std::uint32_t brick = 0; brick < _to.BrickCount(); ++brick)
  {
    const std::array<std::uint64_t, MASK_BRICK_WORDS> bits = _to.BrickBits(brick);
    if (bits != _from.BrickBits(brick))
    {
      diff.bricks.push_back(BrickPatch{brick, bits});
    }
  }
  return diff;
}

bool ApplyDiff(const MaskDiff& _diff, VoxelMask& _mask)
{
  for (const BrickPatch& patch : _diff.bricks)
  {
    if (patch.brick >= _mask.BrickCount())
    {
      return false;
    }
  }
  for (const BrickPatch& patch : _diff.bricks)
  {
    _mask.SetBrickBits(patch.brick, patch.bits);
  }
  _mask.SetLastEvent(_diff.lastEvent);
  return true;
}

std::vector<CellSpan> AbsentSpans(const VoxelMask& _mask, const VoxelMask& _reference)
{
  std::vector<CellSpan> spans;
  if (!(_mask.Size() == _reference.Size()))
  {
    return spans;
  }
  for (std::uint32_t i = 0; i < _reference.CellCount(); ++i)
  {
    if (!_reference.PresentAt(i) || _mask.PresentAt(i))
    {
      continue;
    }
    if (!spans.empty() && spans.back().start + spans.back().count == i)
    {
      ++spans.back().count;
    }
    else
    {
      spans.push_back(CellSpan{i, 1});
    }
  }
  return spans;
}

bool MaskFromAbsentSpans(const VoxelMask& _reference, std::span<const CellSpan> _spans, VoxelMask& _outMask)
{
  VoxelMask mask = _reference;
  for (const CellSpan& span : _spans)
  {
    if (span.count == 0 || span.start >= mask.CellCount() || span.count > mask.CellCount() - span.start)
    {
      return false;
    }
    for (std::uint32_t i = span.start; i < span.start + span.count; ++i)
    {
      mask.SetAt(i, false);
    }
  }
  _outMask = std::move(mask);
  return true;
}

} // namespace Neuron
