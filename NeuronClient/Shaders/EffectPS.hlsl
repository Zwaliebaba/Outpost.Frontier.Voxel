#include "Effect.hlsli"

float4 main(EffectVertexOut _input) : SV_Target0
{
  if ((_input.flags & EFFECT_GLOW) != 0u)
  {
    float r2 = dot(_input.uv, _input.uv);
    if (r2 > 1.0)
    {
      discard;
    }
    float halo = exp(-r2 * 5.0) * (1.0 - r2);
    float core = exp(-r2 * 40.0);
    float3 glow = _input.colorA.rgb * (halo * _input.colorA.a) + _input.colorB.rgb * (core * _input.colorB.a);
    return float4(glow, 0.0);
  }
  float along = _input.uv.x;
  float across = _input.uv.y;
  float4 tint = lerp(_input.colorA, _input.colorB, along);
  float body = exp(-across * across * 3.5) * (1.0 - along);
  float core = exp(-across * across * 26.0) * pow(saturate(1.0 - along), 3.0);
  float3 color = tint.rgb * (tint.a * body) + float3(0.75, 0.90, 1.0) * (core * _input.colorA.a * 0.9);
  return float4(color, 0.0);
}
