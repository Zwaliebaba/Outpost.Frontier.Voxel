#include "RayBox.h"

#include <cmath>

using namespace DirectX;

namespace Neuron
{

namespace
{

[[nodiscard]] float Sign(float _value) noexcept
{
  return _value > 0.0f ? 1.0f : (_value < 0.0f ? -1.0f : 0.0f);
}

[[nodiscard]] float Dot(const XMFLOAT3& _a, const XMFLOAT3& _b) noexcept
{
  return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z;
}

} // namespace

bool IntersectRayBox(const OrientedBox& _box, const Ray& _ray, bool _canStartInBox, float& _outDistance, XMFLOAT3& _outNormal) noexcept
{
  const XMFLOAT3 axisX{_box.axes._11, _box.axes._12, _box.axes._13};
  const XMFLOAT3 axisY{_box.axes._21, _box.axes._22, _box.axes._23};
  const XMFLOAT3 axisZ{_box.axes._31, _box.axes._32, _box.axes._33};

  // 1. The ray in the box's local frame.
  const XMFLOAT3 relative{_ray.origin.x - _box.center.x, _ray.origin.y - _box.center.y, _ray.origin.z - _box.center.z};
  const float origin[3] = {Dot(relative, axisX), Dot(relative, axisY), Dot(relative, axisZ)};
  const float dir[3] = {Dot(_ray.direction, axisX), Dot(_ray.direction, axisY), Dot(_ray.direction, axisZ)};
  const float radius[3] = {_box.radius.x, _box.radius.y, _box.radius.z};

  // Inside the box, the faces that can be hit are the far ones: flip the winding.
  const bool inside = std::fabs(origin[0]) < radius[0] && std::fabs(origin[1]) < radius[1] && std::fabs(origin[2]) < radius[2];
  const float winding = (_canStartInBox && inside) ? -1.0f : 1.0f;

  // 2. The planes of the three potential front faces, and the distance to each.
  float sgn[3];
  float d[3];
  for (int axis = 0; axis < 3; ++axis)
  {
    sgn[axis] = -Sign(dir[axis]);
    d[axis] = (radius[axis] * winding * sgn[axis] - origin[axis]) / dir[axis];
  }

  // 3. Whether each plane hit lies within the face it belongs to.
  const auto test = [&](int _u, int _v, int _w) noexcept
  { return d[_u] >= 0.0f && std::fabs(origin[_v] + dir[_v] * d[_u]) < radius[_v] && std::fabs(origin[_w] + dir[_w] * d[_u]) < radius[_w]; };
  const bool testX = test(0, 1, 2);
  const bool testY = test(1, 2, 0);
  const bool testZ = test(2, 0, 1);

  // 4. Distance and normal from the bitmask.
  float normalLocal[3] = {0.0f, 0.0f, 0.0f};
  if (testX)
  {
    normalLocal[0] = sgn[0];
    _outDistance = d[0];
  }
  else if (testY)
  {
    normalLocal[1] = sgn[1];
    _outDistance = d[1];
  }
  else if (testZ)
  {
    normalLocal[2] = sgn[2];
    _outDistance = d[2];
  }
  else
  {
    return false;
  }

  _outNormal = XMFLOAT3{normalLocal[0] * axisX.x + normalLocal[1] * axisY.x + normalLocal[2] * axisZ.x,
                        normalLocal[0] * axisX.y + normalLocal[1] * axisY.y + normalLocal[2] * axisZ.y,
                        normalLocal[0] * axisX.z + normalLocal[1] * axisY.z + normalLocal[2] * axisZ.z};
  return true;
}

} // namespace Neuron
