#include "HazardWarning.h"

#include <algorithm>
#include <cmath>

namespace Outpost
{

HazardWarning HazardWarningAt(std::span<const Zone> _zones, const UniversePosition& _ship, bool _protected)
{
  HazardWarning warning;
  const HazardExposure exposure = HazardAt(_zones, _ship);
  // On the zone's surface the immersion is nothing, and so is the hazard.
  if (exposure.zone == nullptr || !(exposure.immersion > 0.0f))
  {
    return warning;
  }
  warning.zone = exposure.zone->name;
  warning.immersion = exposure.immersion;
  warning.damaging = !_protected && exposure.damagePerSecond > 0.0f;
  if (exposure.interference > 0.0f)
  {
    // Any interference shows: the outermost of the edge would otherwise round to no warning at all.
    warning.scannerLossPercent = std::clamp(static_cast<int>(std::lround(exposure.interference * 100.0f)), 1, 100);
  }
  return warning;
}

} // namespace Outpost
