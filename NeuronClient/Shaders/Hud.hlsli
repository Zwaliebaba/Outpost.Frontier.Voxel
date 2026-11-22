// The HUD: pixel-space triangles pulled from a structured buffer. Colours arrive sRGB-encoded and
// are converted to linear in the vertex shader, because the render target applies the sRGB curve
// on write. Solid shapes sample a white block in the font atlas, and each vertex names the HUD
// texture it samples and how (ADR-023), so one pipeline draws text, fills and sprites in order.

#ifndef OUTPOST_HUD_HLSLI
#define OUTPOST_HUD_HLSLI

#include "Common.hlsli"

struct HudVertex
{
  float2 position;
  float2 uv;
  uint color;
  uint sampling; // HUD texture slot in the low byte, HUD_SAMPLE_COLOR for RGBA art
};

#define HUD_SLOT_MASK 0xffu
#define HUD_SAMPLE_COLOR 0x100u

StructuredBuffer<HudVertex> g_hudVertices : register(t0);

struct HudVertexOut
{
  float4 position : SV_Position;
  float4 color : COLOR;
  float2 uv : UV;
  nointerpolation uint sampling : SAMPLING;
};

#endif
