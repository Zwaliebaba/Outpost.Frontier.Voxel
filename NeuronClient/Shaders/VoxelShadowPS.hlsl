// The shadow pass: the same ray-box test from the key light's orthographic camera, depth only.

#include "Voxel.hlsli"

float main(VoxelVertexOut _input) : SV_DepthGreaterEqual
{
  // Glazing transmits the key light; the opaque structure behind it still casts a shadow.
  if (g_materials[_input.packed >> 24].albedo.a < 1.0)
  {
    discard;
  }
  float3 origin;
  float3 direction;
  PixelRayVox(_input.position.xy, origin, direction);
  float distance;
  float3 normal;
  if (!IntersectVoxel(_input.centerVox, origin, direction, 1.0 / direction, distance, normal))
  {
    discard;
  }
  float4 clip = mul(float4(origin + direction * distance, 1.0), g_voxToClip);
  return clip.z / clip.w;
}
