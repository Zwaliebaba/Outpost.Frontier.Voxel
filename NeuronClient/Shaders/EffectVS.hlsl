#include "Effect.hlsli"

EffectVertexOut main(uint _vertexId : SV_VertexID)
{
  EffectVertexOut output = (EffectVertexOut)0;
  Effect effect = g_effects[_vertexId / 6u];
  uint corner = _vertexId % 6u;
  const float2 QUAD[6] = {float2(-1, -1), float2(-1, 1), float2(1, -1), float2(-1, 1), float2(1, 1), float2(1, -1)};
  float2 q = QUAD[corner];
  output.colorA = effect.colorA;
  output.colorB = effect.colorB;
  output.flags = effect.flags;

  if ((effect.flags & EFFECT_GLOW) != 0u)
  {
    output.uv = q;
    if ((effect.flags & EFFECT_WIDTH_IN_PIXELS) != 0u)
    {
      float4 clip = mul(float4(effect.a, 1.0), g_viewProjection);
      output.position = clip + float4(q * effect.widthA / (g_viewportSize * 0.5) * clip.w, 0.0, 0.0);
    }
    else
    {
      float3 right = normalize(cross(float3(0.0, 1.0, 0.0), g_cameraForward));
      float3 up = cross(g_cameraForward, right);
      float3 world = effect.a + (right * q.x + up * q.y) * effect.widthA;
      output.position = mul(float4(world, 1.0), g_viewProjection);
    }
    return output;
  }

  // A beam: a quad from a to b that turns about its axis to face the eye, widthA wide at a and
  // widthB at b. The a end is pushed back by its width so the nozzle end is soft, not cut.
  float along = q.x * 0.5 + 0.5;
  float3 axis = effect.b - effect.a;
  float3 axisDirection = normalize(axis);
  float3 center = effect.a + axis * along - axisDirection * ((1.0 - along) * effect.widthA * 0.6);
  float3 toEye = g_isOrthographic > 0.5 ? -g_cameraForward : normalize(g_cameraPosition - center);
  float3 side = cross(axisDirection, toEye);
  float sideLength = length(side);
  side = sideLength > 1e-5 ? side / sideLength : float3(1.0, 0.0, 0.0);
  float width = lerp(effect.widthA, effect.widthB, along);
  output.position = mul(float4(center + side * (q.y * width), 1.0), g_viewProjection);
  output.uv = float2(along, q.y);
  return output;
}
