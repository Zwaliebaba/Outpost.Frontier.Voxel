#pragma once

#include "UniverseObject.h"
#include "Zone.h"

#include <span>
#include <string>

namespace Outpost
{

/// What the flight HUD warns of where the own ship is (plan task 2.8; blueprint ch. 01 §3.5). The
/// warning is the client's own (ch. 01 §3.11): it is worked out from the zones both halves load and
/// the ship's position, and it decides nothing. The damage a hazard does is the server's (AGENTS.md
/// R19) and arrives with slice 3; until a ship carries a scanner, interference is this read-out and
/// hides nothing (GameDesign.md D21).
struct HazardWarning
{
  std::string zone;           // the physical zone the ship is in, by name (R24); empty outside any
  float immersion = 0.0f;     // how deep the ship is in it, 0 to 1
  bool damaging = false;      // the zone damages a ship here
  int scannerLossPercent = 0; // the share of scanner range its interference takes here, 0 to 100

  /// Whether there is anything to warn of.
  [[nodiscard]] bool Active() const noexcept
  {
    return damaging || scannerLossPercent > 0;
  }

  friend bool operator==(const HazardWarning&, const HazardWarning&) = default;
};

/// The warning for a ship at _ship: its physical zone, the smallest zone with damage or interference
/// that contains it, weighed by its immersion there. A protected ship (D16) accumulates no damage
/// (ch. 01 §3.5), so it is warned of none; interference blinds it all the same.
[[nodiscard]] HazardWarning HazardWarningAt(std::span<const Zone> _zones, const UniversePosition& _ship, bool _protected);

} // namespace Outpost
