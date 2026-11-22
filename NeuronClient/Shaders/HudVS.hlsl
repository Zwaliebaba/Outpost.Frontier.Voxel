#include "Hud.hlsli"

float3 SrgbToLinear(float3 _c)
{
  return select(_c <= 0.04045, _c / 12.92, pow((_c + 0.055) / 1.055, 2.4));
}

HudVertexOut main(uint _vertexId : SV_VertexID)
{
  HudVertex vertex = g_hudVertices[_vertexId];
  HudVertexOut output;
  float2 ndc = (vertex.position - g_viewportOrigin) / g_viewportSize * float2(2.0, -2.0) + float2(-1.0, 1.0);
  output.position = float4(ndc, 0.0, 1.0);
  float4 color = float4(vertex.color & 0xffu, (vertex.color >> 8) & 0xffu, (vertex.color >> 16) & 0xffu, vertex.color >> 24) / 255.0;
  output.color = float4(SrgbToLinear(color.rgb), color.a);
  output.uv = vertex.uv;
  output.sampling = vertex.sampling;
  return output;
}
