// Ray-box voxel splatting after Majercik, Crassin, Shirley and McGuire, "A Ray-Box Intersection
// Algorithm and Efficient Dynamic Voxel Rendering", JCGT 7(3), 2018 (Design/Majercik2018Voxel.pdf).
//
// Every surface voxel is drawn as one screen-aligned quad that bounds the projection of its
// bounding sphere (the paper's Listing 4, rewritten for a rectangle instead of a square point).
// The pixel shader casts the pixel's ray against the voxel's box in VOX space (Listing 5), discards
// a miss, and writes the hit's depth. The quad sits at the depth of the sphere's nearest point, so
// the true depth is never in front of it and SV_DepthGreaterEqual keeps early depth rejection.

#ifndef OUTPOST_VOXEL_HLSLI
#define OUTPOST_VOXEL_HLSLI

#include "Common.hlsli"

cbuffer VoxelDraw : register(b1)
{
  row_major float4x4 g_voxToWorld;
  row_major float4x4 g_voxToClip;
  row_major float4x4 g_clipToVox;
  float3 g_cameraForwardVox;
  float g_voxelWorldSize;
  float3 g_cameraPositionVox;
  float g_emissionScale;
  float4 g_tint;
  float3 g_rimColor;
  float g_rimStrength;
};

struct Material
{
  float4 albedo;
  float4 emission;
};

StructuredBuffer<uint2> g_voxels : register(t0);
StructuredBuffer<Material> g_materials : register(t1);

struct VoxelVertexOut
{
  // Conservative depth output requires centroid, non-perspective SV_Position.
  linear noperspective centroid float4 position : SV_Position;
  nointerpolation float3 centerVox : CENTER;
  nointerpolation uint packed : PACKED;     // colour << 24 | exposed faces
  nointerpolation uint occlusion : OCCLUSION;
};

static const float SQRT3_HALF = 0.8660254;

// Listing 5, specialised to an axis-aligned unit box in VOX space: no rotation, radius 0.5.
// Returns the entry distance along the ray and the outward normal of the face that was entered.
bool IntersectVoxel(float3 _center, float3 _origin, float3 _direction, float3 _invDirection, out float _distance, out float3 _normal)
{
  const float3 radius = 0.5;
  float3 origin = _origin - _center;
  float3 sgn = -sign(_direction);
  // Outside the box the near planes are the ones facing the ray (winding +1).
  float3 distanceToPlane = (radius * sgn - origin) * _invDirection;
  bool3 test = bool3(
      (distanceToPlane.x >= 0.0) && all(abs(origin.yz + _direction.yz * distanceToPlane.x) < radius.yz),
      (distanceToPlane.y >= 0.0) && all(abs(origin.zx + _direction.zx * distanceToPlane.y) < radius.zx),
      (distanceToPlane.z >= 0.0) && all(abs(origin.xy + _direction.xy * distanceToPlane.z) < radius.xy));
  sgn = test.x ? float3(sgn.x, 0.0, 0.0) : (test.y ? float3(0.0, sgn.y, 0.0) : float3(0.0, 0.0, test.z ? sgn.z : 0.0));
  _distance = (sgn.x != 0.0) ? distanceToPlane.x : ((sgn.y != 0.0) ? distanceToPlane.y : distanceToPlane.z);
  _normal = sgn;
  return any(sgn != 0.0);
}

// The face index (FaceDirection order: +X, -X, +Y, -Y, +Z, -Z) of an axis normal.
uint FaceIndex(float3 _normal)
{
  if (_normal.x != 0.0)
  {
    return _normal.x > 0.0 ? 0u : 1u;
  }
  if (_normal.y != 0.0)
  {
    return _normal.y > 0.0 ? 2u : 3u;
  }
  return _normal.z > 0.0 ? 4u : 5u;
}

float Hash(uint _value)
{
  _value ^= _value >> 16;
  _value *= 0x7feb352du;
  _value ^= _value >> 15;
  _value *= 0x846ca68bu;
  _value ^= _value >> 16;
  return float(_value & 0xffffffu) / 16777216.0;
}

// The ray through this pixel, in VOX space.
void PixelRayVox(float2 _pixel, out float3 _origin, out float3 _direction)
{
  float2 ndc = PixelToNdc(_pixel);
  float4 nearPoint = mul(float4(ndc, 0.0, 1.0), g_clipToVox);
  float4 farPoint = mul(float4(ndc, 1.0, 1.0), g_clipToVox);
  _origin = nearPoint.xyz / nearPoint.w;
  _direction = normalize(farPoint.xyz / farPoint.w - _origin);
}

VoxelVertexOut VoxelVertex(uint _corner, uint _instance)
{
  VoxelVertexOut output = (VoxelVertexOut)0;
  uint2 voxel = g_voxels[_instance];
  float3 center = float3(voxel.x & 0xffu, (voxel.x >> 8) & 0xffu, (voxel.x >> 16) & 0xffu) + 0.5;
  uint faces = voxel.y & 0x3fu;
  output.centerVox = center;
  output.packed = (voxel.x & 0xff000000u) | faces;
  output.occlusion = voxel.y >> 8;

  // Back-face rejection: a voxel whose every exposed face looks away from the eye cannot be hit.
  float3 toEye = g_isOrthographic > 0.5 ? -g_cameraForwardVox : (g_cameraPositionVox - center);
  uint facing = (toEye.x > 0.5 ? 1u : 0u) | (toEye.x < -0.5 ? 2u : 0u) | (toEye.y > 0.5 ? 4u : 0u) | (toEye.y < -0.5 ? 8u : 0u) |
                (toEye.z > 0.5 ? 16u : 0u) | (toEye.z < -0.5 ? 32u : 0u);
  if (g_isOrthographic > 0.5)
  {
    facing = (toEye.x > 0.0 ? 1u : 2u) | (toEye.y > 0.0 ? 4u : 8u) | (toEye.z > 0.0 ? 16u : 32u);
  }
  if ((faces & facing) == 0u)
  {
    output.position = float4(0.0, 0.0, -1.0, 1.0); // outside the clip volume: culled
    return output;
  }

  // Listing 4: the clip-space bounds of the bounding sphere's projection, via its quadric.
  const float radius = SQRT3_HALF;
  float4 columnX = float4(g_voxToClip._11, g_voxToClip._21, g_voxToClip._31, g_voxToClip._41);
  float4 columnY = float4(g_voxToClip._12, g_voxToClip._22, g_voxToClip._32, g_voxToClip._42);
  float4 columnW = float4(g_voxToClip._14, g_voxToClip._24, g_voxToClip._34, g_voxToClip._44);
  float4 c = float4(center, 1.0);
  float x0 = dot(c, columnX);
  float y0 = dot(c, columnY);
  float w0 = dot(c, columnW);
  float3 ax = columnX.xyz * radius;
  float3 ay = columnY.xyz * radius;
  float3 aw = columnW.xyz * radius;

  // Conservative near-plane rejection: the whole sphere must be in front of the eye.
  if (w0 - length(aw) <= g_nearZ * 0.999)
  {
    output.position = float4(0.0, 0.0, -1.0, 1.0);
    return output;
  }
  float denominator = dot(aw, aw) - w0 * w0;
  float2 centerNdc = float2(dot(ax, aw) - x0 * w0, dot(ay, aw) - y0 * w0) / denominator;
  float2 quadricW = float2(dot(ax, ax) - x0 * x0, dot(ay, ay) - y0 * y0) / denominator;
  float2 halfNdc = sqrt(max(centerNdc * centerNdc - quadricW, 0.0));

  // Large on screen: the eight box corners bound the voxel more tightly than its sphere.
  float2 halfPixels = halfNdc * g_viewportSize * 0.5;
  if (max(halfPixels.x, halfPixels.y) > 12.0)
  {
    float2 lo = 1e9;
    float2 hi = -1e9;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
      float3 corner = center + (float3(i & 1u, (i >> 1) & 1u, (i >> 2) & 1u) - 0.5);
      float4 clip = mul(float4(corner, 1.0), g_voxToClip);
      float2 ndc = clip.xy / clip.w;
      lo = min(lo, ndc);
      hi = max(hi, ndc);
    }
    centerNdc = (lo + hi) * 0.5;
    halfNdc = (hi - lo) * 0.5;
    halfPixels = halfNdc * g_viewportSize * 0.5;
  }

  // Stochastic pruning of subpixel voxels: keep each with probability equal to its pixel
  // coverage, so a distant surface keeps its average density without drawing every voxel.
  float coverage = 4.0 * halfPixels.x * halfPixels.y;
  if (coverage < 0.35 && Hash(_instance * 2654435761u + 17u) > coverage / 0.35)
  {
    output.position = float4(0.0, 0.0, -1.0, 1.0);
    return output;
  }

  // Pad by half a pixel so the rasterizer never misses a sliver at the quad edge.
  halfNdc += 1.0 / g_viewportSize;

  // Depth of the sphere's nearest point along the view direction.
  float3 nearest = center - g_cameraForwardVox * radius;
  float4 nearestClip = mul(float4(nearest, 1.0), g_voxToClip);
  float depth = saturate(nearestClip.z / nearestClip.w);

  float2 cornerSign = float2((_corner & 1u) ? 1.0 : -1.0, (_corner & 2u) ? -1.0 : 1.0);
  output.position = float4(centerNdc + cornerSign * halfNdc, depth, 1.0);
  return output;
}

#endif
