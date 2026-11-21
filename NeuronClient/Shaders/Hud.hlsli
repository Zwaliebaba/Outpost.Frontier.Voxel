// The HUD: pixel-space triangles pulled from a structured buffer. Colours arrive sRGB-encoded and
// are converted to linear in the vertex shader, because the render target applies the sRGB curve
// on write. Solid shapes sample a white block in the atlas, so one pipeline draws everything.

#ifndef OUTPOST_HUD_HLSLI
#define OUTPOST_HUD_HLSLI

#include "Common.hlsli"

struct HudVertex
{
  float2 position;
  float2 uv;
  uint color;
  uint padding;
};

StructuredBuffer<HudVertex> g_hudVertices : register(t0);

struct HudVertexOut
{
  float4 position : SV_Position;
  float4 color : COLOR;
  float2 uv : UV;
};

#endif
