#include "Zone.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Outpost
{

namespace
{

/// The point in the zone's own frame, its centre at the origin.
[[nodiscard]] XMFLOAT3 Local(const Zone& _zone, const WorldPosition& _point) noexcept
{
  const XMVECTOR offset = XMVectorSet(static_cast<float>(_point.x - _zone.position.x), static_cast<float>(_point.y - _zone.position.y),
                                      static_cast<float>(_point.z - _zone.position.z), 0.0f);
  XMFLOAT3 local{};
  XMStoreFloat3(&local, XMVector3InverseRotate(offset, XMLoadFloat4(&_zone.orientation)));
  return local;
}

/// The zone's half extents grown by a margin: sphere radius, box half edges, cylinder radius and half
/// length.
[[nodiscard]] XMFLOAT3 HalfExtents(const Zone& _zone, float _marginMetres) noexcept
{
  switch (_zone.shape)
  {
  case ZoneShape::Box:
    return XMFLOAT3{_zone.sizeMetres.x * 0.5f + _marginMetres, _zone.sizeMetres.y * 0.5f + _marginMetres,
                    _zone.sizeMetres.z * 0.5f + _marginMetres};
  case ZoneShape::Cylinder:
    return XMFLOAT3{_zone.sizeMetres.x + _marginMetres, _zone.sizeMetres.y * 0.5f + _marginMetres, 0.0f};
  case ZoneShape::Sphere:
    break;
  }
  return XMFLOAT3{_zone.sizeMetres.x + _marginMetres, 0.0f, 0.0f};
}

[[nodiscard]] float Ratio(float _distance, float _extent) noexcept
{
  if (_extent > 0.0f)
  {
    return _distance / _extent;
  }
  return _distance > 0.0f ? INFINITY : 0.0f;
}

[[nodiscard]] float RatioWithin(const Zone& _zone, const WorldPosition& _point, float _marginMetres) noexcept
{
  const XMFLOAT3 local = Local(_zone, _point);
  const XMFLOAT3 half = HalfExtents(_zone, _marginMetres);
  switch (_zone.shape)
  {
  case ZoneShape::Box:
    return std::max({Ratio(std::abs(local.x), half.x), Ratio(std::abs(local.y), half.y), Ratio(std::abs(local.z), half.z)});
  case ZoneShape::Cylinder:
    return std::max(Ratio(std::hypot(local.x, local.z), half.x), Ratio(std::abs(local.y), half.y));
  case ZoneShape::Sphere:
    break;
  }
  return Ratio(std::sqrt(local.x * local.x + local.y * local.y + local.z * local.z), half.x);
}

} // namespace

ZoneProperty Zone::Properties() const noexcept
{
  ZoneProperty properties = ZoneProperty::None;
  if (Physical())
  {
    properties = properties | ZoneProperty::Hazard;
  }
  if (!region.empty())
  {
    properties = properties | ZoneProperty::Region;
  }
  return properties;
}

float BoundingRadius(const Zone& _zone) noexcept
{
  const XMFLOAT3& size = _zone.sizeMetres;
  switch (_zone.shape)
  {
  case ZoneShape::Box:
    return 0.5f * std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
  case ZoneShape::Cylinder:
    return std::hypot(size.x, size.y * 0.5f);
  case ZoneShape::Sphere:
    break;
  }
  return size.x;
}

float SurfaceRatio(const Zone& _zone, const WorldPosition& _point) noexcept
{
  return RatioWithin(_zone, _point, 0.0f);
}

bool Contains(const Zone& _zone, const WorldPosition& _point, float _marginMetres) noexcept
{
  return RatioWithin(_zone, _point, _marginMetres) <= 1.0f;
}

float Immersion(const Zone& _zone, const WorldPosition& _point) noexcept
{
  const float ratio = SurfaceRatio(_zone, _point);
  if (ratio >= 1.0f)
  {
    return 0.0f;
  }
  if (ratio <= 1.0f - _zone.edgeFraction)
  {
    return 1.0f;
  }
  return (1.0f - ratio) / _zone.edgeFraction;
}

const Zone* SmallestZoneAt(std::span<const Zone> _zones, const WorldPosition& _point, ZoneProperty _required) noexcept
{
  const Zone* smallest = nullptr;
  float smallestRadius = INFINITY;
  for (const Zone& zone : _zones)
  {
    const auto required = static_cast<std::uint8_t>(_required);
    if ((static_cast<std::uint8_t>(zone.Properties()) & required) != required || !Contains(zone, _point))
    {
      continue;
    }
    const float radius = BoundingRadius(zone);
    if (radius < smallestRadius)
    {
      smallest = &zone;
      smallestRadius = radius;
    }
  }
  return smallest;
}

ZoneProperty PropertiesAround(std::span<const Zone> _zones, const WorldPosition& _point, float _radiusMetres) noexcept
{
  ZoneProperty properties = ZoneProperty::None;
  for (const Zone& zone : _zones)
  {
    if (Contains(zone, _point, _radiusMetres))
    {
      properties = properties | zone.Properties();
    }
  }
  return properties;
}

HazardExposure HazardAt(std::span<const Zone> _zones, const WorldPosition& _point) noexcept
{
  HazardExposure exposure;
  exposure.zone = SmallestZoneAt(_zones, _point, ZoneProperty::Hazard);
  if (exposure.zone != nullptr)
  {
    exposure.immersion = Immersion(*exposure.zone, _point);
    exposure.damagePerSecond = exposure.zone->damagePerSecond * exposure.immersion;
    exposure.interference = std::min(1.0f, exposure.zone->interference * exposure.immersion);
  }
  return exposure;
}

} // namespace Outpost
