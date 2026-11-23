#pragma once

#include "ClientCombat.h"
#include "MaskEvent.h"
#include "VoxelMesher.h"
#include "VoxModel.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Outpost
{

/// The surface a damaged object is drawn with in place of its shared model (ADR-013).
struct MaskedSurface
{
  ObjectId object = NO_OBJECT;
  Neuron::VoxelSurface surface; // its voxels and bounds; no faces are built
  std::uint64_t generation = 0; // raised whenever the surface is rebuilt, which asks for an upload
  bool ready = false;           // every brick has been extracted at least once
};

/// Surfaces for the objects whose masks the client holds (plan task 3.9, ADR-013). Each object's
/// appearance is its archetype's colours, with its scene model's colours where that model has a cell
/// (the frigate's scorch, D25), less the cells its mask lacks. The appearance is cut into bricks of
/// Neuron::MASK_BRICK_CELLS on an edge; a change marks the bricks it touches, and the neighbours whose
/// faces or occlusion read it, and only those are extracted again, at most a budget of bricks a frame,
/// the caller's order first. An object's surface is rebuilt from its bricks once none is waiting, so
/// a frame never draws half an event.
class MaskedSurfaces
{
public:
  /// Bricks re-extracted per frame at most: F6 measured 0.07 to 0.10 ms a brick on one core, so
  /// twenty is about 2 ms (ADR-013). A brick that is empty before and after costs nothing.
  static constexpr std::size_t BRICKS_PER_FRAME = 20;

  /// _sceneModels[i] is archetype i's scene model voxels, or null when the archetype is its own.
  void Load(std::shared_ptr<const CombatContent> _content, std::vector<const Neuron::VoxModel*> _sceneModels);

  /// Takes every change the masks hold, then extracts at most _budget waiting bricks, objects in
  /// _order first and the rest after. Surfaces whose objects lost their masks go. The bricks
  /// extracted, counting only those with a surface before or after.
  std::size_t Update(ClientCombat& _combat, std::span<const ObjectId> _order, std::size_t _budget = BRICKS_PER_FRAME);

  [[nodiscard]] const MaskedSurface* Find(ObjectId _object) const noexcept;
  /// Bricks waiting across every object.
  [[nodiscard]] std::size_t WaitingBricks() const noexcept;

  /// The appearance of an archetype before any mask: its colours, with its scene model's where that
  /// model has a cell of the same grid.
  [[nodiscard]] static Neuron::VoxModel BaseAppearance(const VoxelArchetype& _archetype, const Neuron::VoxModel* _sceneModel);

private:
  struct Entry
  {
    std::size_t archetype = 0;
    std::uint64_t revision = 0;
    Neuron::VoxModel appearance;
    Neuron::Int3 brickGrid{0, 0, 0};
    std::vector<Neuron::VoxelSurface> bricks;
    std::vector<std::uint8_t> waiting; // one a brick
    std::size_t waitingCount = 0;
    bool rebuild = false;
    MaskedSurface drawn;
  };

  [[nodiscard]] const Neuron::VoxModel& Base(std::size_t _archetype);
  void Take(Entry& _entry, ClientMask& _mask);
  void Mark(Entry& _entry, const Neuron::Int3& _cell);
  [[nodiscard]] std::size_t Extract(Entry& _entry, std::size_t _budget);
  static void Rebuild(Entry& _entry);

  std::shared_ptr<const CombatContent> m_content;
  std::vector<const Neuron::VoxModel*> m_sceneModels;
  std::vector<std::optional<Neuron::VoxModel>> m_bases;
  std::vector<Entry> m_entries;
};

} // namespace Outpost
