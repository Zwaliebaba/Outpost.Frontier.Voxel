// Shades a voxel: the pixel's ray against the voxel's box (Voxel.hlsli), then the key light with
// shadow, a sky/ground ambient, the scene's point lights, the material table's emission, and a
// selection rim. Writes the hit's own depth.

#include "Voxel.hlsli"

struct PixelOut
{
  float4 color : SV_Target0;
  float depth : SV_DepthGreaterEqual;
};

float ShadowFactor(float3 _worldPosition, float3 _normal)
{
  if (g_shadowEnabled < 0.5)
  {
    return 1.0;
  }
  float4 clip = mul(float4(_worldPosition + _normal * 0.6, 1.0), g_shadowViewProjection);
  float3 ndc = clip.xyz / clip.w;
  float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
  if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
  {
    return 1.0;
  }
  float reference = ndc.z - 0.0015;
  float sum = 0.0;
  [unroll]
  for (int y = -1; y <= 1; ++y)
  {
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
      sum += g_shadowMap.SampleCmpLevelZero(g_shadowSampler, uv + float2(x, y) * g_shadowTexelSize, reference);
    }
  }
  return sum / 9.0;
}

PixelOut main(VoxelVertexOut _input)
{
  float3 origin;
  float3 direction;
  PixelRayVox(_input.position.xy, origin, direction);
  float distance;
  float3 normalVox;
  if (!IntersectVoxel(_input.centerVox, origin, direction, 1.0 / direction, distance, normalVox))
  {
    discard;
  }
  float3 hitVox = origin + direction * distance;
  float4 hitClip = mul(float4(hitVox, 1.0), g_voxToClip);

  uint colorIndex = _input.packed >> 24;
  uint face = FaceIndex(normalVox);
  Material material = g_materials[colorIndex];

  float3 worldPosition = mul(float4(hitVox, 1.0), g_voxToWorld).xyz;
  float3 normal = normalize(mul(float4(normalVox, 0.0), g_voxToWorld).xyz);
  float3 toEye = g_isOrthographic > 0.5 ? -g_cameraForward : normalize(g_cameraPosition - worldPosition);

  // Per-voxel variation, recessed-face occlusion and a soft bevel at the voxel's edges give the
  // blocks their read without any extra geometry.
  uint3 cell = uint3(_input.centerVox);
  float variation = 0.93 + 0.14 * Hash(cell.x * 73856093u ^ cell.y * 19349663u ^ cell.z * 83492791u);
  float occlusion = 1.0 - 0.17 * float((_input.occlusion >> (face * 2u)) & 3u);
  float3 local = abs(hitVox - _input.centerVox);
  float2 tangent = normalVox.x != 0.0 ? local.yz : (normalVox.y != 0.0 ? local.zx : local.xy);
  float bevel = lerp(0.80, 1.0, smoothstep(0.0, 0.09, 0.5 - max(tangent.x, tangent.y)));
  float3 albedo = material.albedo.rgb * variation * g_tint.rgb;

  float keyDiffuse = saturate(dot(normal, g_keyLightDirection)) * ShadowFactor(worldPosition, normal);
  float3 light = g_keyLightColor * keyDiffuse;
  light += lerp(g_ambientGround, g_ambientSky, normal.y * 0.5 + 0.5) * occlusion;

  uint lightCount = (uint)g_pointLightCount;
  for (uint i = 0; i < lightCount; ++i)
  {
    float3 toLight = g_pointLights[i].position - worldPosition;
    float lightDistance = length(toLight);
    float falloff = saturate(1.0 - lightDistance / g_pointLights[i].radius);
    float wrap = saturate(dot(normal, toLight / max(lightDistance, 1e-3)) * 0.75 + 0.25);
    light += g_pointLights[i].color * (g_pointLights[i].intensity * falloff * falloff * wrap);
  }

  // A cool sheen on faces turned toward the key light reads as metal.
  float3 halfVector = normalize(g_keyLightDirection + toEye);
  float specular = pow(saturate(dot(normal, halfVector)), 24.0) * 0.18 * keyDiffuse;

  float3 color = albedo * light * occlusion * bevel + g_keyLightColor * specular;
  color += material.emission.rgb * g_emissionScale * lerp(0.85, 1.0, bevel);
  color += g_rimColor * (pow(1.0 - saturate(dot(normal, toEye)), 3.0) * g_rimStrength);

  // Distance haze separates the foreground ships from the shipyard and the rock behind it.
  float haze = 1.0 - exp(-length(worldPosition - g_cameraPosition) / 2600.0);
  color = lerp(color, float3(0.010, 0.016, 0.030), haze * 0.55 * (1.0 - g_isOrthographic));

  PixelOut output;
  output.color = float4(ToneMap(color), 1.0);
  output.depth = hitClip.z / hitClip.w;
  return output;
}
