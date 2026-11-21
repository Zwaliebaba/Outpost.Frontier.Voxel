// Additive light effects: soft glows on lamps, beacons and stars, and tapered beams for engine
// plumes. Effects are depth-tested against the voxels but never write depth.

#ifndef OUTPOST_EFFECT_HLSLI
#define OUTPOST_EFFECT_HLSLI

#include "Common.hlsli"

#define EFFECT_WIDTH_IN_PIXELS 1u
#define EFFECT_GLOW 2u

struct Effect
{
  float3 a;
  float widthA;
  float3 b;
  float widthB;
  float4 colorA;
  float4 colorB;
  uint flags;
  float3 padding;
};

StructuredBuffer<Effect> g_effects : register(t0);

struct EffectVertexOut
{
  float4 position : SV_Position;
  float4 colorA : COLOR0;
  float4 colorB : COLOR1;
  float2 uv : UV; // glow: -1..1 square; beam: x along 0..1, y across -1..1
  nointerpolation uint flags : FLAGS;
};

#endif
