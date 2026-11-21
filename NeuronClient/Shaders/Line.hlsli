// World-space line segments, expanded in screen space to a constant pixel width and depth-tested
// against the scene. Used for the blueprint wireframe and the selected-ship silhouette.

#ifndef OUTPOST_LINE_HLSLI
#define OUTPOST_LINE_HLSLI

#include "Common.hlsli"

struct LineSegment
{
  float3 a;
  float widthPixels;
  float3 b;
  float padding;
  float4 color;
};

StructuredBuffer<LineSegment> g_segments : register(t0);

struct LineVertexOut
{
  float4 position : SV_Position;
  float4 color : COLOR;
  float across : ACROSS; // in units of the line's half width; |across| > 1 is the soft fringe
};

#endif
