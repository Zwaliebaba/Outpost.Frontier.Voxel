#include "HudBatch.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Neuron
{

using DirectX::XMFLOAT2;

std::uint32_t FadeColor(std::uint32_t _color, float _alpha) noexcept
{
  const float alpha = static_cast<float>(_color >> 24) * std::clamp(_alpha, 0.0f, 1.0f);
  return (_color & 0x00ffffffu) | (static_cast<std::uint32_t>(std::lround(alpha)) << 24);
}

void HudBatch::Begin(const FontAtlas* _atlas) noexcept
{
  m_atlas = _atlas;
  for (auto& layer : m_vertices)
  {
    layer.clear();
  }
  m_layer = BACK;
  m_whiteU = _atlas != nullptr ? _atlas->WhiteU() : 0.0f;
  m_whiteV = _atlas != nullptr ? _atlas->WhiteV() : 0.0f;
}

void HudBatch::Push(float _x, float _y, float _u, float _v, std::uint32_t _color)
{
  m_vertices[m_layer].push_back(GpuHudVertex{XMFLOAT2{_x, _y}, XMFLOAT2{_u, _v}, _color, 0});
}

void HudBatch::Rect(float _x, float _y, float _width, float _height, std::uint32_t _color)
{
  RectGradient(_x, _y, _width, _height, _color, _color);
}

void HudBatch::RectGradient(float _x, float _y, float _width, float _height, std::uint32_t _top, std::uint32_t _bottom)
{
  if (_width <= 0.0f || _height <= 0.0f)
  {
    return;
  }
  const float x0 = _x;
  const float y0 = _y;
  const float x1 = _x + _width;
  const float y1 = _y + _height;
  PushSolid(x0, y0, _top);
  PushSolid(x1, y0, _top);
  PushSolid(x0, y1, _bottom);
  PushSolid(x1, y0, _top);
  PushSolid(x1, y1, _bottom);
  PushSolid(x0, y1, _bottom);
}

void HudBatch::Outline(float _x, float _y, float _width, float _height, float _thickness, std::uint32_t _color)
{
  Rect(_x, _y, _width, _thickness, _color);
  Rect(_x, _y + _height - _thickness, _width, _thickness, _color);
  Rect(_x, _y + _thickness, _thickness, _height - 2.0f * _thickness, _color);
  Rect(_x + _width - _thickness, _y + _thickness, _thickness, _height - 2.0f * _thickness, _color);
}

void HudBatch::Line(float _x0, float _y0, float _x1, float _y1, float _width, std::uint32_t _color)
{
  const float dx = _x1 - _x0;
  const float dy = _y1 - _y0;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length < 1e-4f)
  {
    return;
  }
  // Axis-aligned lines are plain rectangles: crisp, no fringe needed.
  if (std::abs(dx) < 1e-4f || std::abs(dy) < 1e-4f)
  {
    const float half = _width * 0.5f;
    Rect(std::min(_x0, _x1) - (std::abs(dx) < 1e-4f ? half : 0.0f), std::min(_y0, _y1) - (std::abs(dy) < 1e-4f ? half : 0.0f),
         std::abs(dx) < 1e-4f ? _width : std::abs(dx), std::abs(dy) < 1e-4f ? _width : std::abs(dy), _color);
    return;
  }
  const float nx = -dy / length;
  const float ny = dx / length;
  const float core = std::max(_width * 0.5f - 0.5f, 0.0f);
  const float outer = core + 1.0f;
  const std::uint32_t clear = _color & 0x00ffffffu;
  auto strip = [&](float _inner, float _outerOffset, std::uint32_t _innerColor, std::uint32_t _outerColor, float _sign)
  {
    const float ix = nx * _inner * _sign;
    const float iy = ny * _inner * _sign;
    const float ox = nx * _outerOffset * _sign;
    const float oy = ny * _outerOffset * _sign;
    PushSolid(_x0 + ix, _y0 + iy, _innerColor);
    PushSolid(_x1 + ix, _y1 + iy, _innerColor);
    PushSolid(_x0 + ox, _y0 + oy, _outerColor);
    PushSolid(_x1 + ix, _y1 + iy, _innerColor);
    PushSolid(_x1 + ox, _y1 + oy, _outerColor);
    PushSolid(_x0 + ox, _y0 + oy, _outerColor);
  };
  strip(0.0f, core, _color, _color, 1.0f);
  strip(0.0f, core, _color, _color, -1.0f);
  strip(core, outer, _color, clear, 1.0f);
  strip(core, outer, _color, clear, -1.0f);
}

void HudBatch::Triangle(XMFLOAT2 _a, XMFLOAT2 _b, XMFLOAT2 _c, std::uint32_t _color)
{
  PushSolid(_a.x, _a.y, _color);
  PushSolid(_b.x, _b.y, _color);
  PushSolid(_c.x, _c.y, _color);
}

void HudBatch::Polygon(std::span<const XMFLOAT2> _points, std::uint32_t _color)
{
  if (_points.size() < 3)
  {
    return;
  }
  XMFLOAT2 centroid{0.0f, 0.0f};
  for (const XMFLOAT2& point : _points)
  {
    centroid.x += point.x;
    centroid.y += point.y;
  }
  centroid.x /= static_cast<float>(_points.size());
  centroid.y /= static_cast<float>(_points.size());
  const std::uint32_t clear = _color & 0x00ffffffu;
  for (std::size_t i = 0; i < _points.size(); ++i)
  {
    const XMFLOAT2 a = _points[i];
    const XMFLOAT2 b = _points[(i + 1) % _points.size()];
    Triangle(centroid, a, b, _color);
    // Rim fringe on slanted edges only; axis-aligned edges sit on pixel boundaries.
    if (std::abs(a.x - b.x) > 1e-3f && std::abs(a.y - b.y) > 1e-3f)
    {
      const float dx = b.x - a.x;
      const float dy = b.y - a.y;
      const float length = std::sqrt(dx * dx + dy * dy);
      float nx = -dy / length;
      float ny = dx / length;
      const float midX = (a.x + b.x) * 0.5f - centroid.x;
      const float midY = (a.y + b.y) * 0.5f - centroid.y;
      if (nx * midX + ny * midY < 0.0f)
      {
        nx = -nx;
        ny = -ny;
      }
      PushSolid(a.x, a.y, _color);
      PushSolid(b.x, b.y, _color);
      PushSolid(a.x + nx, a.y + ny, clear);
      PushSolid(b.x, b.y, _color);
      PushSolid(b.x + nx, b.y + ny, clear);
      PushSolid(a.x + nx, a.y + ny, clear);
    }
  }
}

void HudBatch::PolyLine(std::span<const XMFLOAT2> _points, bool _closed, float _width, std::uint32_t _color)
{
  if (_points.size() < 2)
  {
    return;
  }
  const std::size_t count = _closed ? _points.size() : _points.size() - 1;
  for (std::size_t i = 0; i < count; ++i)
  {
    const XMFLOAT2 a = _points[i];
    const XMFLOAT2 b = _points[(i + 1) % _points.size()];
    Line(a.x, a.y, b.x, b.y, _width, _color);
  }
}

void HudBatch::Circle(float _centerX, float _centerY, float _radius, float _width, std::uint32_t _color, int _segments)
{
  std::vector<XMFLOAT2> points;
  points.reserve(static_cast<std::size_t>(_segments));
  for (int i = 0; i < _segments; ++i)
  {
    const float angle = static_cast<float>(i) / static_cast<float>(_segments) * 2.0f * std::numbers::pi_v<float>;
    points.push_back(XMFLOAT2{_centerX + std::cos(angle) * _radius, _centerY + std::sin(angle) * _radius});
  }
  PolyLine(points, true, _width, _color);
}

void HudBatch::Disc(float _centerX, float _centerY, float _radius, std::uint32_t _color, int _segments)
{
  std::vector<XMFLOAT2> points;
  points.reserve(static_cast<std::size_t>(_segments));
  for (int i = 0; i < _segments; ++i)
  {
    const float angle = static_cast<float>(i) / static_cast<float>(_segments) * 2.0f * std::numbers::pi_v<float>;
    points.push_back(XMFLOAT2{_centerX + std::cos(angle) * _radius, _centerY + std::sin(angle) * _radius});
  }
  Polygon(points, _color);
}

float HudBatch::TextWidth(std::size_t _face, std::string_view _text) const noexcept
{
  return m_atlas != nullptr ? static_cast<float>(m_atlas->MeasureText(_face, _text)) : 0.0f;
}

float HudBatch::LineHeight(std::size_t _face) const noexcept
{
  return m_atlas != nullptr && _face < m_atlas->FaceCount() ? static_cast<float>(m_atlas->Face(_face).lineHeight) : 0.0f;
}

float HudBatch::Text(std::size_t _face, float _x, float _y, std::string_view _text, std::uint32_t _color, TextAlign _align)
{
  if (m_atlas == nullptr || _face >= m_atlas->FaceCount())
  {
    return 0.0f;
  }
  const float width = TextWidth(_face, _text);
  float pen = _x;
  if (_align == TextAlign::Center)
  {
    pen -= width * 0.5f;
  }
  else if (_align == TextAlign::Right)
  {
    pen -= width;
  }
  pen = std::round(pen);
  const float top = std::round(_y);
  const FontFace& face = m_atlas->Face(_face);
  for (const char character : _text)
  {
    if (character < FIRST_GLYPH || character > LAST_GLYPH)
    {
      continue;
    }
    const Glyph& glyph = face.glyphs[static_cast<std::size_t>(character - FIRST_GLYPH)];
    if (character != ' ')
    {
      const float x0 = pen + static_cast<float>(glyph.offsetX);
      const float x1 = x0 + static_cast<float>(glyph.width);
      const float y1 = top + static_cast<float>(glyph.height);
      Push(x0, top, glyph.u0, glyph.v0, _color);
      Push(x1, top, glyph.u1, glyph.v0, _color);
      Push(x0, y1, glyph.u0, glyph.v1, _color);
      Push(x1, top, glyph.u1, glyph.v0, _color);
      Push(x1, y1, glyph.u1, glyph.v1, _color);
      Push(x0, y1, glyph.u0, glyph.v1, _color);
    }
    pen += static_cast<float>(glyph.advance);
  }
  return width;
}

} // namespace Neuron
