#include "Collision.h"

#include "ModelAxes.h"
#include "SceneDefinition.h"
#include "ShipArchetype.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace DirectX;

namespace Outpost
{

namespace
{

[[nodiscard]] float Component(const XMFLOAT3& _vector, int _axis) noexcept
{
  return _axis == 0 ? _vector.x : (_axis == 1 ? _vector.y : _vector.z);
}

} // namespace

bool SweepSphere(const WorldPosition& _start, const XMFLOAT3& _motion, float _radiusMetres, const CollisionBox& _box,
                 SweepHit& _outHit) noexcept
{
  // Into the box's frame: the offset is taken in double and only then narrowed (ADR-007).
  const XMVECTOR inverse = XMQuaternionConjugate(XMLoadFloat4(&_box.orientation));
  const XMVECTOR offset = XMVectorSet(static_cast<float>(_start.x - _box.center.x), static_cast<float>(_start.y - _box.center.y),
                                      static_cast<float>(_start.z - _box.center.z), 0.0f);
  XMFLOAT3 start{};
  XMFLOAT3 motion{};
  XMStoreFloat3(&start, XMVector3Rotate(offset, inverse));
  XMStoreFloat3(&motion, XMVector3Rotate(XMLoadFloat3(&_motion), inverse));
  const XMFLOAT3 half{_box.halfExtentsMetres.x + _radiusMetres, _box.halfExtentsMetres.y + _radiusMetres,
                      _box.halfExtentsMetres.z + _radiusMetres};

  bool inside = true;
  float enter = -std::numeric_limits<float>::max();
  float exit = std::numeric_limits<float>::max();
  int enterAxis = 0;
  for (int axis = 0; axis < 3; ++axis)
  {
    const float p = Component(start, axis);
    const float d = Component(motion, axis);
    const float h = Component(half, axis);
    inside = inside && std::abs(p) < h;
    if (std::abs(d) < 1.0e-9f)
    {
      if (std::abs(p) >= h)
      {
        return false; // parallel to this slab and outside it
      }
      continue;
    }
    const float t0 = (-h - p) / d;
    const float t1 = (h - p) / d;
    const float entering = std::min(t0, t1);
    const float leaving = std::max(t0, t1);
    if (entering > enter)
    {
      enter = entering;
      enterAxis = axis;
    }
    exit = std::min(exit, leaving);
  }
  if (inside || enter > exit || enter < 0.0f || enter > 1.0f)
  {
    return false;
  }
  // The face met is the one on the side the sphere came from.
  XMFLOAT3 localNormal{0.0f, 0.0f, 0.0f};
  const float side = Component(start, enterAxis) < 0.0f ? -1.0f : 1.0f;
  (enterAxis == 0 ? localNormal.x : (enterAxis == 1 ? localNormal.y : localNormal.z)) = side;
  _outHit.fraction = enter;
  XMStoreFloat3(&_outHit.normal, XMVector3Rotate(XMLoadFloat3(&localNormal), XMLoadFloat4(&_box.orientation)));
  _outHit.body = &_box;
  return true;
}

bool SweepSphere(const WorldPosition& _start, const XMFLOAT3& _motion, float _radiusMetres, std::span<const CollisionBox> _boxes,
                 ObjectId _self, SweepHit& _outHit) noexcept
{
  bool found = false;
  for (const CollisionBox& box : _boxes)
  {
    SweepHit hit;
    if (box.object != _self && SweepSphere(_start, _motion, _radiusMetres, box, hit) && (!found || hit.fraction < _outHit.fraction))
    {
      _outHit = hit;
      found = true;
    }
  }
  return found;
}

CollisionBox BoxForObject(const SceneDefinition& _scene, const WorldObject& _object) noexcept
{
  CollisionBox box;
  box.object = _object.id;
  box.center = _object.pose.position;
  box.orientation = _object.pose.orientation;
  const ModelAsset* asset = _scene.FindModel(_object.model);
  if (asset == nullptr)
  {
    return box;
  }
  // The grid's corners in model space, where the VOX-to-model mapping keeps the box axis-aligned.
  const XMFLOAT3 size{static_cast<float>(asset->size.x), static_cast<float>(asset->size.y), static_cast<float>(asset->size.z)};
  const XMFLOAT3 low = Neuron::VoxPointToModel(XMFLOAT3{0.0f, 0.0f, 0.0f}, asset->pivotVox, asset->voxelSize);
  const XMFLOAT3 high = Neuron::VoxPointToModel(size, asset->pivotVox, asset->voxelSize);
  const float scale = _object.scale;
  box.halfExtentsMetres =
    XMFLOAT3{std::abs(high.x - low.x) * 0.5f * scale, std::abs(high.y - low.y) * 0.5f * scale, std::abs(high.z - low.z) * 0.5f * scale};
  const XMVECTOR centerModel =
    XMVectorScale(XMVectorSet((low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f, (low.z + high.z) * 0.5f, 0.0f), scale);
  XMFLOAT3 centerOffset{};
  XMStoreFloat3(&centerOffset, XMVector3Rotate(centerModel, XMLoadFloat4(&_object.pose.orientation)));
  box.center.x += static_cast<double>(centerOffset.x);
  box.center.y += static_cast<double>(centerOffset.y);
  box.center.z += static_cast<double>(centerOffset.z);
  return box;
}

float BoundingRadius(const CollisionBox& _box) noexcept
{
  return XMVectorGetX(XMVector3Length(XMLoadFloat3(&_box.halfExtentsMetres)));
}

const ShipArchetype* FlyingArchetype(const ShipContent& _content, const WorldObject& _object) noexcept
{
  const auto found = std::ranges::find(_content.ships, _object.model, &ShipArchetype::model);
  return found != _content.ships.end() ? &*found : nullptr;
}

std::vector<CollisionBox> StaticBodies(const SceneDefinition& _scene, const ShipContent& _content, std::span<const WorldObject> _objects)
{
  std::vector<CollisionBox> bodies;
  for (const WorldObject& object : _objects)
  {
    const SceneInstance* instance = _scene.FindInstance(object.name);
    if (FlyingArchetype(_content, object) != nullptr || (instance != nullptr && instance->style == InstanceStyle::Blueprint))
    {
      continue;
    }
    bodies.push_back(BoxForObject(_scene, object));
  }
  return bodies;
}

void Bounce(XMFLOAT3& _velocity, const XMFLOAT3& _normal) noexcept
{
  const XMVECTOR velocity = XMLoadFloat3(&_velocity);
  const XMVECTOR normal = XMVector3Normalize(XMLoadFloat3(&_normal));
  const float into = XMVectorGetX(XMVector3Dot(velocity, normal));
  if (into >= 0.0f)
  {
    return;
  }
  const XMVECTOR normalPart = XMVectorScale(normal, into);
  const XMVECTOR tangent = XMVectorSubtract(velocity, normalPart);
  const float tangentSpeed = XMVectorGetX(XMVector3Length(tangent));
  // Coulomb friction: the tangential change is at most the friction times the normal impulse.
  const float impulse = (1.0f + COLLISION_ELASTICITY) * -into;
  const float kept = tangentSpeed > 0.0f ? std::max(0.0f, tangentSpeed - COLLISION_FRICTION * impulse) / tangentSpeed : 0.0f;
  XMStoreFloat3(&_velocity, XMVectorAdd(XMVectorScale(tangent, kept), XMVectorScale(normalPart, -COLLISION_ELASTICITY)));
}

float CollisionDamage(float _relativeSpeedMetresPerSecond, float _massA, float _massB) noexcept
{
  float ratio = 1.0f;
  if (_massA > 0.0f && _massB > 0.0f)
  {
    ratio = std::min(1.0f, _massA / _massB);
  }
  else if (_massA > 0.0f && _massB <= 0.0f)
  {
    ratio = 0.0f; // a ship cannot hurt an immovable body by ramming it
  }
  return 0.5f * _relativeSpeedMetresPerSecond * ratio;
}

} // namespace Outpost
