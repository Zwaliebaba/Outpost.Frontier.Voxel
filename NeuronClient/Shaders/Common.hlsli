// Shared declarations for every Outpost:Frontier shader. The C++ mirrors are in GpuTypes.h; the
// root signature is built in Renderer.cpp:
//   b0 FrameConstants, b1 per-draw constants, t0 / t1 root SRVs, t2 shadow map, t3 HUD atlas.

#ifndef OUTPOST_COMMON_HLSLI
#define OUTPOST_COMMON_HLSLI

#define MAX_POINT_LIGHTS 16

struct PointLight
{
  float3 position;
  float radius;
  float3 color;
  float intensity;
};

cbuffer FrameConstants : register(b0)
{
  row_major float4x4 g_viewProjection;
  row_major float4x4 g_inverseViewProjection;
  row_major float4x4 g_shadowViewProjection;
  float3 g_cameraPosition;
  float g_time;
  float2 g_viewportOrigin;
  float2 g_viewportSize;
  float3 g_keyLightDirection;
  float g_shadowEnabled;
  float3 g_keyLightColor;
  float g_pointLightCount;
  float3 g_ambientSky;
  float g_exposure;
  float3 g_ambientGround;
  float g_shadowTexelSize;
  float3 g_cameraForward;
  float g_isOrthographic;
  float g_nearZ;
  float g_renderTargetHeight;
  float g_padding0;
  float g_padding1;
  PointLight g_pointLights[MAX_POINT_LIGHTS];
};

Texture2D<float> g_shadowMap : register(t2);
Texture2D<float> g_hudAtlas : register(t3);
SamplerComparisonState g_shadowSampler : register(s0);
SamplerState g_linearClamp : register(s1);

// Pixel position (SV_Position.xy) to normalized device coordinates in the current viewport.
float2 PixelToNdc(float2 _pixel)
{
  float2 uv = (_pixel - g_viewportOrigin) / g_viewportSize;
  return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

// A filmic curve (Narkowicz's ACES fit) after exposure. The render target is sRGB, so the
// hardware applies the transfer function on write.
float3 ToneMap(float3 _linear)
{
  float3 x = _linear * g_exposure;
  return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

#endif
