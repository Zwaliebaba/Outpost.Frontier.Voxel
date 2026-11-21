#pragma once

#include "SceneDefinition.h"
#include "WorldSnapshot.h"

#include <cstdint>
#include <vector>

namespace Outpost
{

/// The authoritative game state: the server half of the sample. It is built from the scene's
/// command records, advanced in whole ticks, and read by the client only through a snapshot, so
/// the client never touches it directly — the seam a network transport would later occupy.
/// Combat, damage and navigation are out of scope; what the server runs is resource income.
class World
{
public:
  void Load(const SceneDefinition& _scene);
  void Tick();

  [[nodiscard]] std::uint64_t TickCount() const noexcept
  {
    return m_snapshot.tick;
  }
  [[nodiscard]] const WorldSnapshot& Snapshot() const noexcept
  {
    return m_snapshot;
  }

private:
  struct Income
  {
    std::int64_t perMinute = 0;
    std::int64_t remainder = 0; // income owed but not yet whole, in 1/ticks-per-minute units
  };

  WorldSnapshot m_snapshot;
  std::vector<Income> m_income;
};

} // namespace Outpost
