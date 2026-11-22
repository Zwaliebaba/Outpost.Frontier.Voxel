#pragma once

#include "UniverseObject.h"

#include <DirectXMath.h>

#include <cstdint>
#include <span>
#include <string>

namespace Outpost
{

// Zones (plan task 2.1, blueprint ch. 01 §3.4 and §3.5): passive volumes of space with properties.
// A zone does nothing itself; the systems that ask about it do. "Smallest zone wins" is the only rule
// for overlap, which makes nesting work: a small hazard inside a large region wins without either
// knowing of the other.

enum class ZoneShape : std::uint8_t
{
  Sphere,  // size.x: the radius
  Box,     // size: the three full edge lengths
  Cylinder // size.x: the radius; size.y: the length along the zone's own Y axis
};

/// What a zone is for: a bit set, merged over the zones around a point.
enum class ZoneProperty : std::uint8_t
{
  None = 0,
  Hazard = 1, // damage or scanner interference: a physical zone
  Region = 2  // a named region, for the location read-out
};

[[nodiscard]] constexpr ZoneProperty operator|(ZoneProperty _a, ZoneProperty _b) noexcept
{
  return static_cast<ZoneProperty>(static_cast<std::uint8_t>(_a) | static_cast<std::uint8_t>(_b));
}

[[nodiscard]] constexpr bool HasProperty(ZoneProperty _set, ZoneProperty _property) noexcept
{
  return (static_cast<std::uint8_t>(_set) & static_cast<std::uint8_t>(_property)) != 0;
}

struct Zone
{
  std::string name;
  ZoneShape shape = ZoneShape::Sphere;
  UniversePosition position;
  DirectX::XMFLOAT4 orientation{0.0f, 0.0f, 0.0f, 1.0f}; // zone frame to world
  DirectX::XMFLOAT3 sizeMetres{0.0f, 0.0f, 0.0f};
  float edgeFraction = 0.0f;    // the outer band where immersion ramps from 1 to 0, as a share of the size
  float damagePerSecond = 0.0f; // hit points per second at full immersion (ch. 01 §3.5)
  float interference = 0.0f;    // scanner interference at full immersion, 0 to 1
  std::string region;           // the name a named region shows; empty for none

  [[nodiscard]] ZoneProperty Properties() const noexcept;
  /// Only damage or interference makes a zone physical.
  [[nodiscard]] bool Physical() const noexcept
  {
    return damagePerSecond > 0.0f || interference > 0.0f;
  }
};

/// The radius of the sphere around the zone's centre that holds it (ch. 01 §3.4's table): what
/// "smallest" means.
[[nodiscard]] float BoundingRadius(const Zone& _zone) noexcept;

/// The point's distance from the centre over the centre-to-surface distance in its direction: 0 at
/// the centre, 1 on the surface.
[[nodiscard]] float SurfaceRatio(const Zone& _zone, const UniversePosition& _point) noexcept;

/// Whether the point lies inside the zone grown by _marginMetres on every side.
[[nodiscard]] bool Contains(const Zone& _zone, const UniversePosition& _point, float _marginMetres = 0.0f) noexcept;

/// How deep the point lies in the zone (ch. 01 §3.4): 0 outside, 1 in the core, and across the outer
/// band (1 - c) / edge, with c the surface ratio. With edge 0 the zone has a hard border.
[[nodiscard]] float Immersion(const Zone& _zone, const UniversePosition& _point) noexcept;

/// The smallest zone that contains the point and has every property asked for (None: any zone),
/// or null. Ties go to the zone listed first.
[[nodiscard]] const Zone* SmallestZoneAt(std::span<const Zone> _zones, const UniversePosition& _point,
                                         ZoneProperty _required = ZoneProperty::None) noexcept;

/// The properties of every zone touching a sphere of _radiusMetres around the point, merged.
[[nodiscard]] ZoneProperty PropertiesAround(std::span<const Zone> _zones, const UniversePosition& _point, float _radiusMetres) noexcept;

/// What a hazard does to a ship at a point (ch. 01 §3.5): its physical zone, the smallest zone with
/// damage or interference that contains it, and its immersion there.
struct HazardExposure
{
  const Zone* zone = nullptr;
  float immersion = 0.0f;
  float damagePerSecond = 0.0f; // the zone's rate times the immersion
  float interference = 0.0f;    // min(1, interference times immersion): scanner range is multiplied by 1 less this
};

[[nodiscard]] HazardExposure HazardAt(std::span<const Zone> _zones, const UniversePosition& _point) noexcept;

} // namespace Outpost
