#include "Line.hlsli"

// Additive, premultiplied: overlapping blueprint edges brighten like a hologram.
float4 main(LineVertexOut _input) : SV_Target0
{
  float distanceOut = abs(_input.across);
  float coverage = distanceOut <= 1.0 ? 1.0 : saturate(2.0 - distanceOut);
  float strength = _input.color.a * coverage;
  return float4(_input.color.rgb * strength, strength);
}
