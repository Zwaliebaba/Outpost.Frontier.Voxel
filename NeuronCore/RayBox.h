#pragma once

#include <DirectXMath.h>

namespace Neuron
{

struct Ray
{
  DirectX::XMFLOAT3 origin;
  DirectX::XMFLOAT3 direction;
};

/// A box by centre, half-extents and orientation. The rows of `axes` are the box's local X, Y and
/// Z axes expressed in the space the ray is in (orthonormal).
struct OrientedBox
{
  DirectX::XMFLOAT3 center;
  DirectX::XMFLOAT3 radius;
  DirectX::XMFLOAT3X3 axes;
};

/// The ray/box intersection of Majercik, Crassin, Shirley and McGuire, "A Ray-Box Intersection
/// Algorithm and Efficient Dynamic Voxel Rendering", JCGT 7(3), 2018, Listing 5 — the same test
/// the voxel pixel shader runs (VoxelCommon.hlsli), here on the CPU for picking.
///
/// Only the three faces that can face the ray are tested, and the first one whose hit lies inside
/// the box is the answer, because a box is convex. _outDistance is in units of _ray.direction.
[[nodiscard]] bool IntersectRayBox(const OrientedBox& _box, const Ray& _ray, bool _canStartInBox, float& _outDistance,
                                   DirectX::XMFLOAT3& _outNormal) noexcept;

} // namespace Neuron
