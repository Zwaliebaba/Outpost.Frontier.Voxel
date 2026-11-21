#include "Hud.hlsli"

float4 main(HudVertexOut _input) : SV_Target0
{
  float coverage = g_hudAtlas.SampleLevel(g_linearClamp, _input.uv, 0.0);
  return float4(_input.color.rgb, _input.color.a * coverage);
}
