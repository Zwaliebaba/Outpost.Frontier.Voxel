#include "ScreenProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace DirectX;

namespace Neuron
{

namespace
{

constexpr std::array<std::array<int, 2>, 12> BOX_EDGES = {{
  {0, 1},
  {1, 3},
  {3, 2},
  {2, 0}, // bottom
  {4, 5},
  {5, 7},
  {7, 6},
  {6, 4}, // top
  {0, 4},
  {1, 5},
  {2, 6},
  {3, 7}, // verticals
}};

} // namespace

std::array<XMFLOAT3, 8> BoxCorners(const OrientedBox& _box) noexcept
{
  std::array<XMFLOAT3, 8> corners{};
  const XMVECTOR center = XMLoadFloat3(&_box.center);
  const XMVECTOR axisX = XMVectorScale(XMVectorSet(_box.axes._11, _box.axes._12, _box.axes._13, 0.0f), _box.radius.x);
  const XMVECTOR axisY = XMVectorScale(XMVectorSet(_box.axes._21, _box.axes._22, _box.axes._23, 0.0f), _box.radius.y);
  const XMVECTOR axisZ = XMVectorScale(XMVectorSet(_box.axes._31, _box.axes._32, _box.axes._33, 0.0f), _box.radius.z);
  for (int i = 0; i < 8; ++i)
  {
    // Bit 0 picks the X side, bit 1 Y, bit 2 Z.
    XMVECTOR corner = center;
    corner = XMVectorAdd(corner, XMVectorScale(axisX, (i & 1) != 0 ? 1.0f : -1.0f));
    corner = XMVectorAdd(corner, XMVectorScale(axisY, (i & 2) != 0 ? 1.0f : -1.0f));
    corner = XMVectorAdd(corner, XMVectorScale(axisZ, (i & 4) != 0 ? 1.0f : -1.0f));
    XMStoreFloat3(&corners[static_cast<std::size_t>(i)], corner);
  }
  return corners;
}

ProjectedBounds ProjectBox(const std::array<XMFLOAT3, 8>& _cornersWorld, FXMMATRIX _viewProjection, float _viewportWidth,
                           float _viewportHeight) noexcept
{
  ProjectedBounds result;
  std::array<XMFLOAT4, 8> clip{};
  XMVECTOR centerSum = XMVectorZero();
  for (std::size_t i = 0; i < 8; ++i)
  {
    const XMVECTOR c = XMVector4Transform(XMVectorSetW(XMLoadFloat3(&_cornersWorld[i]), 1.0f), _viewProjection);
    XMStoreFloat4(&clip[i], c);
    centerSum = XMVectorAdd(centerSum, c);
  }

  // Direction toward the object, for indicators at the screen edge. Raw clip x and y keep the
  // right sign even behind the camera, where dividing by w would mirror them.
  XMFLOAT4 center{};
  XMStoreFloat4(&center, XMVectorScale(centerSum, 1.0f / 8.0f));
  const float dirX = center.x * _viewportWidth;
  const float dirY = -center.y * _viewportHeight;
  const float length = std::sqrt(dirX * dirX + dirY * dirY);
  if (length > 1e-6f)
  {
    result.edgeDirection = XMFLOAT2{dirX / length, dirY / length};
  }

  float minX = std::numeric_limits<float>::max();
  float minY = std::numeric_limits<float>::max();
  float maxX = std::numeric_limits<float>::lowest();
  float maxY = std::numeric_limits<float>::lowest();
  int projected = 0;
  const auto addPoint = [&](const XMFLOAT4& _p) noexcept
  {
    const float invW = 1.0f / _p.w;
    const float px = (_p.x * invW * 0.5f + 0.5f) * _viewportWidth;
    const float py = (0.5f - _p.y * invW * 0.5f) * _viewportHeight;
    minX = std::min(minX, px);
    maxX = std::max(maxX, px);
    minY = std::min(minY, py);
    maxY = std::max(maxY, py);
    ++projected;
  };

  // Direct3D clip space: in front of the near plane is z >= 0.
  for (const XMFLOAT4& p : clip)
  {
    if (p.z >= 0.0f && p.w > 0.0f)
    {
      addPoint(p);
    }
    else
    {
      result.clippedByNearPlane = true;
    }
  }
  if (result.clippedByNearPlane)
  {
    for (const auto& edge : BOX_EDGES)
    {
      const XMFLOAT4& a = clip[static_cast<std::size_t>(edge[0])];
      const XMFLOAT4& b = clip[static_cast<std::size_t>(edge[1])];
      if ((a.z >= 0.0f) == (b.z >= 0.0f))
      {
        continue;
      }
      const float t = a.z / (a.z - b.z);
      const XMFLOAT4 crossing{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, 0.0f, a.w + (b.w - a.w) * t};
      if (crossing.w > 0.0f)
      {
        addPoint(crossing);
      }
    }
  }

  if (projected == 0)
  {
    result.visibility = ScreenVisibility::BehindCamera;
    return result;
  }
  result.rect = ScreenRect{minX, minY, maxX, maxY};
  const bool overlaps = maxX >= 0.0f && minX <= _viewportWidth && maxY >= 0.0f && minY <= _viewportHeight;
  result.visibility = overlaps ? ScreenVisibility::OnScreen : ScreenVisibility::OffScreen;
  return result;
}

Ray PickRay(float _pixelX, float _pixelY, FXMMATRIX _inverseViewProjection, float _viewportWidth, float _viewportHeight) noexcept
{
  const float ndcX = (_pixelX + 0.5f) / _viewportWidth * 2.0f - 1.0f;
  const float ndcY = 1.0f - (_pixelY + 0.5f) / _viewportHeight * 2.0f;
  const XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), _inverseViewProjection);
  const XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), _inverseViewProjection);
  Ray ray{};
  XMStoreFloat3(&ray.origin, nearPoint);
  XMStoreFloat3(&ray.direction, XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint)));
  return ray;
}

} // namespace Neuron
