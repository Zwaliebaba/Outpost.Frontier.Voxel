#include "Line.hlsli"

// Moves a clip-space endpoint that is behind the near plane along the segment until it is not.
float4 ClipToNear(float4 _point, float4 _other)
{
  if (g_isOrthographic > 0.5 || _point.w >= g_nearZ)
  {
    return _point;
  }
  float t = (g_nearZ - _point.w) / (_other.w - _point.w);
  return lerp(_point, _other, t);
}

LineVertexOut main(uint _vertexId : SV_VertexID)
{
  LineVertexOut output = (LineVertexOut)0;
  LineSegment segment = g_segments[_vertexId / 6u];
  uint corner = _vertexId % 6u;
  float4 clipA = mul(float4(segment.a, 1.0), g_viewProjection);
  float4 clipB = mul(float4(segment.b, 1.0), g_viewProjection);
  if (g_isOrthographic < 0.5 && clipA.w < g_nearZ && clipB.w < g_nearZ)
  {
    output.position = float4(0.0, 0.0, -1.0, 1.0);
    return output;
  }
  float4 a = ClipToNear(clipA, clipB);
  float4 b = ClipToNear(clipB, clipA);

  float2 pixelA = a.xy / a.w * g_viewportSize * 0.5;
  float2 pixelB = b.xy / b.w * g_viewportSize * 0.5;
  float2 along = pixelB - pixelA;
  float lengthPixels = length(along);
  along = lengthPixels > 1e-4 ? along / lengthPixels : float2(1.0, 0.0);
  float2 normal = float2(-along.y, along.x);

  // Two triangles, (a-, a+, b-) and (a+, b+, b-), widened by a pixel for the soft fringe and
  // lengthened by half the width at each end so that joints close.
  const uint END[6] = {0u, 0u, 1u, 0u, 1u, 1u};
  const float SIDE[6] = {-1.0, 1.0, -1.0, 1.0, 1.0, -1.0};
  float halfCore = max(segment.widthPixels * 0.5, 0.5);
  float halfWidth = halfCore + 1.0;
  bool atA = END[corner] == 0u;
  float4 base = atA ? a : b;
  float2 offsetPixels = normal * SIDE[corner] * halfWidth + along * (atA ? -halfCore : halfCore);
  output.position = base + float4(offsetPixels / (g_viewportSize * 0.5) * base.w, 0.0, 0.0);
  output.color = segment.color;
  output.across = SIDE[corner] * halfWidth / halfCore;
  return output;
}
