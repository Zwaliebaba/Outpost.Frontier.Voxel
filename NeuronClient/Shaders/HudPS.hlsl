#include "Hud.hlsli"

// Straight alpha out, blended SRC_ALPHA / INV_SRC_ALPHA. Coverage (text, fills, R8 masks) scales
// the tint's alpha; colour art, decoded from sRGB by its texture format, multiplies the tint.
float4 main(HudVertexOut _input) : SV_Target0
{
  uint slot = _input.sampling & HUD_SLOT_MASK;
  float4 texel = g_hudTextures[NonUniformResourceIndex(slot)].SampleLevel(g_linearClamp, _input.uv, 0.0);
  if ((_input.sampling & HUD_SAMPLE_COLOR) != 0u)
  {
    return _input.color * texel;
  }
  return float4(_input.color.rgb, _input.color.a * texel.r);
}
