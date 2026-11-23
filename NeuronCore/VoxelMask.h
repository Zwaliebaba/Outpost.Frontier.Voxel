#pragma once

#include "VoxModel.h"
#include "VoxelMesher.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Neuron
{

// The voxel mask (ADR-012): one bit per cell of an archetype model's grid, present or not. Colour
// and material stay with the archetype, so a mask says only which of its cells are still there.
// A cell's index is x + sizeX * (y + sizeY * z), the same index the hull fixtures use (ADR-021).

inline constexpr int MASK_BRICK_CELLS = 16;         // a brick's edge, in cells (ADR-012, from F6's measurements)
inline constexpr std::size_t MASK_BRICK_WORDS = 64; // 16^3 bits in 64-bit words

class VoxelMask
{
public:
  VoxelMask() = default;
  /// Every cell absent.
  explicit VoxelMask(const Int3& _size);
  /// Present wherever the model has a solid cell.
  [[nodiscard]] static VoxelMask FromModel(const VoxModel& _model);

  [[nodiscard]] const Int3& Size() const noexcept
  {
    return m_size;
  }
  [[nodiscard]] std::uint32_t CellCount() const noexcept
  {
    return m_cellCount;
  }
  [[nodiscard]] bool Contains(int _x, int _y, int _z) const noexcept;
  [[nodiscard]] std::uint32_t Index(int _x, int _y, int _z) const noexcept;
  [[nodiscard]] Int3 CellOf(std::uint32_t _index) const noexcept;

  /// False for any cell outside the grid.
  [[nodiscard]] bool Present(int _x, int _y, int _z) const noexcept;
  [[nodiscard]] bool PresentAt(std::uint32_t _index) const noexcept;
  void SetAt(std::uint32_t _index, bool _present) noexcept;

  [[nodiscard]] std::uint32_t PresentCount() const noexcept
  {
    return m_presentCount;
  }

  /// The last mask event applied to this mask, 0 for none: what names a snapshot or a diff (R22).
  [[nodiscard]] std::uint64_t LastEvent() const noexcept
  {
    return m_lastEvent;
  }
  void SetLastEvent(std::uint64_t _event) noexcept
  {
    m_lastEvent = _event;
  }

  /// FNV-1a over the size and the bits: two masks hash equal exactly when they hold the same cells.
  /// The last event is not hashed.
  [[nodiscard]] std::uint64_t Hash() const noexcept;

  [[nodiscard]] std::span<const std::uint64_t> Words() const noexcept
  {
    return m_words;
  }

  /// The grid of 16-cell bricks that covers the mask, rounded up on every axis.
  [[nodiscard]] Int3 BrickGrid() const noexcept;
  [[nodiscard]] std::uint32_t BrickCount() const noexcept;
  /// The 4,096 bits of one brick, cell (lx, ly, lz) of the brick at bit lx + 16 * (ly + 16 * lz).
  /// Cells outside the grid read as absent.
  [[nodiscard]] std::array<std::uint64_t, MASK_BRICK_WORDS> BrickBits(std::uint32_t _brick) const noexcept;
  void SetBrickBits(std::uint32_t _brick, const std::array<std::uint64_t, MASK_BRICK_WORDS>& _bits) noexcept;

  friend bool operator==(const VoxelMask& _a, const VoxelMask& _b) noexcept
  {
    return _a.m_size == _b.m_size && _a.m_words == _b.m_words;
  }

private:
  Int3 m_size{0, 0, 0};
  std::uint32_t m_cellCount = 0;
  std::uint32_t m_presentCount = 0;
  std::uint64_t m_lastEvent = 0;
  std::vector<std::uint64_t> m_words;
};

/// A whole mask as of an event: what a client that has never seen the object starts from.
struct MaskSnapshot
{
  std::uint64_t lastEvent = 0;
  Int3 size{0, 0, 0};
  std::vector<std::uint64_t> words;

  friend bool operator==(const MaskSnapshot&, const MaskSnapshot&) = default;
};

[[nodiscard]] MaskSnapshot TakeSnapshot(const VoxelMask& _mask);
/// False, changing nothing, for a snapshot whose words do not fit its size.
[[nodiscard]] bool RestoreSnapshot(const MaskSnapshot& _snapshot, VoxelMask& _outMask);

/// One brick's bits.
struct BrickPatch
{
  std::uint32_t brick = 0;
  std::array<std::uint64_t, MASK_BRICK_WORDS> bits{};

  friend bool operator==(const BrickPatch&, const BrickPatch&) = default;
};

/// The bricks in which two masks of one archetype differ, and the last event the newer includes:
/// what brings a client from the older to the newer without sending the whole mask (R22).
struct MaskDiff
{
  std::uint64_t lastEvent = 0;
  std::vector<BrickPatch> bricks; // in ascending brick order

  friend bool operator==(const MaskDiff&, const MaskDiff&) = default;
};

/// Empty bricks list when the masks hold the same cells. The masks must have the same size.
[[nodiscard]] MaskDiff DiffBricks(const VoxelMask& _from, const VoxelMask& _to);
/// False, changing nothing, when a brick lies outside the mask.
[[nodiscard]] bool ApplyDiff(const MaskDiff& _diff, VoxelMask& _mask);

/// A run of cell indices, [start, start + count).
struct CellSpan
{
  std::uint32_t start = 0;
  std::uint32_t count = 0;

  friend bool operator==(const CellSpan&, const CellSpan&) = default;
};

/// The cells present in _reference and absent from _mask, as sorted runs: how a save stores a mask
/// against its archetype (ADR-011, as amended by ADR-012).
[[nodiscard]] std::vector<CellSpan> AbsentSpans(const VoxelMask& _mask, const VoxelMask& _reference);
/// _reference with the spans' cells removed. False for a span outside the grid.
[[nodiscard]] bool MaskFromAbsentSpans(const VoxelMask& _reference, std::span<const CellSpan> _spans, VoxelMask& _outMask);

} // namespace Neuron
