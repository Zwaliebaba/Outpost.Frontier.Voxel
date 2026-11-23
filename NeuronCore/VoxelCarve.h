#pragma once

#include "VoxModel.h"
#include "VoxelMask.h"
#include "VoxelMesher.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

// The mask's events (ADR-012, AGENTS.md R22). A mask changes only by these functions, applied in
// integer arithmetic to integer inputs, so every machine that applies the same event to the same
// mask removes or restores the same cells. The authority decides an event; both halves apply it.

/// A material strength that no energy overcomes.
inline constexpr std::uint32_t UNBREAKABLE = 0xFFFFFFFFu;

/// The energy each colour index costs to remove, from the archetype's palette roles.
using StrengthTable = std::array<std::uint32_t, 256>;

enum class CarveShape : std::uint8_t
{
  Sphere, // the cells within the radius of the impact cell
  Capsule // spheres along the shot, from the impact cell for lengthCells steps
};

struct CarveSpec
{
  std::uint64_t sequence = 0; // the event's number on this mask; the mask records it
  Int3 cell{0, 0, 0};         // the impact cell, in the archetype's VOX coordinates
  Int3 direction{0, 0, 0};    // capsule: the shot's direction in VOX axes, integer, not zero
  CarveShape shape = CarveShape::Sphere;
  std::uint8_t radiusCells = 1;
  std::uint8_t lengthCells = 1; // capsule: the number of sphere centers along the shot
  std::uint32_t energy = 0;     // what removing cells may cost, in strength units
  std::uint32_t seed = 0;       // the event's own: it roughens the edge, and nothing else does

  friend bool operator==(const CarveSpec&, const CarveSpec&) = default;
};

struct CarveResult
{
  std::vector<std::uint32_t> removed; // cell indices, in the order they went
  std::uint32_t energySpent = 0;      // never more than the spec's energy
};

/// Removes cells around the impact (ADR-012). The candidates are the shape's cells, taken in a
/// fixed order: nearest sphere center first, then the squared distance to it, then the cell index.
/// A candidate goes when it is present, exposed (the impact cell itself, or with a face neighbour
/// absent or outside the grid, counting cells this event already removed), and the energy left
/// covers its cost: its material's strength plus a seeded share of up to half of it, which makes the
/// edge ragged. A cell that is not yet exposed when its turn comes stays, so energy does not tunnel
/// behind an unbroken layer. Cells in _toughened, when given, cost _toughFactor times as much. The
/// mask records the spec's sequence.
CarveResult Carve(VoxelMask& _mask, const VoxModel& _archetype, const StrengthTable& _strength, const CarveSpec& _spec,
                  const VoxelMask* _toughened = nullptr, std::uint32_t _toughFactor = 1);

struct PatchSpec
{
  std::uint64_t sequence = 0;
  std::uint32_t maxCells = 0;

  friend bool operator==(const PatchSpec&, const PatchSpec&) = default;
};

/// Restores up to maxCells cells (a repair, ADR-012): in ascending index order, each cell that is
/// absent, allowed, and touches a present face neighbour, counting cells this patch already restored.
/// _allowed is the archetype's cells that this repair may restore. The mask records the sequence.
std::vector<std::uint32_t> Patch(VoxelMask& _mask, const VoxelMask& _allowed, const PatchSpec& _spec);

/// The present cells no longer connected, face to face, to any present anchor cell, grouped into
/// connected parts: each part's cells ascending, the parts ordered by their first cell. With no
/// anchor present every present cell is in a part.
///
/// The contract is ADR-012's: _removed names the cells the last event took, so that a later version
/// can search only near them. In the MVP the function fills from the anchors over the whole mask.
[[nodiscard]] std::vector<std::vector<std::uint32_t>> DetachedParts(const VoxelMask& _mask, std::span<const std::uint32_t> _anchors,
                                                                    std::span<const std::uint32_t> _removed);

/// A stateless integer hash of a value under a seed: the carve's only source of variation.
[[nodiscard]] std::uint32_t HashCell(std::uint32_t _seed, std::uint32_t _value) noexcept;

} // namespace Neuron
