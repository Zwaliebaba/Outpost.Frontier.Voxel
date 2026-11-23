#pragma once

#include "UniverseObject.h"

#include <DirectXMath.h>

#include <cstddef>
#include <span>

namespace Outpost
{

/// Something the player can target, as the client sees it this frame.
struct TargetCandidate
{
  ObjectId object = NO_OBJECT;
  Owner owner = Owner::Neutral;
  UniversePosition position;
  bool ship = false; // a flying ship; otherwise a station or another body
};

/// The player's target and sub-target (plan task 3.10, GameDesign.md D26). T takes the nearest
/// hostile ship in front of the ship, or the nearest at all when none is in front, and from a hostile
/// target goes on to the next nearest, round; a middle click targets what it points at; Ctrl+T
/// clears. [ and ] step through the target's modules, the whole ship included. The target is the
/// client's own: it is what the HUD brackets, and what Dock flies to when it is a station. The
/// server is told nothing of it but that station.
class Targeting
{
public:
  static constexpr int WHOLE_SHIP = -1;

  [[nodiscard]] ObjectId Target() const noexcept
  {
    return m_target;
  }
  /// The module sub-targeted, by its index in the target's modules, or WHOLE_SHIP.
  [[nodiscard]] int Module() const noexcept
  {
    return m_module;
  }

  void Next(std::span<const TargetCandidate> _candidates, const UniversePosition& _from, const DirectX::XMFLOAT3& _forward);
  void Set(ObjectId _object) noexcept;
  void Clear() noexcept;
  /// One step through the modules, _step +1 or -1, from the whole ship round through _moduleCount.
  void CycleModule(int _step, std::size_t _moduleCount) noexcept;
  /// Forgets a target that is no longer among the candidates: destroyed, gone or docked away.
  void Keep(std::span<const TargetCandidate> _candidates) noexcept;

private:
  ObjectId m_target = NO_OBJECT;
  int m_module = WHOLE_SHIP;
};

} // namespace Outpost
