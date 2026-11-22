#pragma once

#include "SceneDefinition.h"
#include "WorldSnapshot.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Outpost
{

/// The authoritative game state: the server half of the game (AGENTS.md R18, R19). It loads its
/// own content, gives every placed instance a world object with an identity and a pose (ADR-009),
/// and advances in whole ticks. Nothing but an Observer reads it on a client's behalf (R25): the
/// client builds its own picture from the messages the observer sends. Today it runs resource
/// income only, which is the ledger the design gives an outpost whose owner is away
/// (Design/GameDesign.md §7.2). Combat, damage and navigation come with the design's later slices.
class World
{
public:
  /// Loads Scene.txt and the model sizes from the asset root. On broken content, false and why.
  [[nodiscard]] bool LoadContent(const std::filesystem::path& _assetRoot, std::string& _outError);

  /// Builds the world from a parsed scene. Attached instances need their models' sizes set
  /// (LoadSceneContent); without them their poses use a pivot at the origin.
  void Load(const SceneDefinition& _scene);
  void Tick();

  [[nodiscard]] std::uint64_t TickCount() const noexcept
  {
    return m_state.tick;
  }
  /// The server's own state. Only the observer encodes it for a client; no client is handed it.
  [[nodiscard]] const WorldSnapshot& State() const noexcept
  {
    return m_state;
  }

private:
  struct Income
  {
    std::int64_t perMinute = 0;
    std::int64_t remainder = 0; // income owed but not yet whole, in 1/ticks-per-minute units
  };

  WorldSnapshot m_state;
  std::vector<Income> m_income;
};

} // namespace Outpost
