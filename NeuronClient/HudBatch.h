#pragma once

#include "FontAtlas.h"
#include "GpuTypes.h"

#include <DirectXMath.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Neuron
{

/// An sRGB colour packed as the HUD shader reads it: r | g << 8 | b << 16 | a << 24.
[[nodiscard]] constexpr std::uint32_t Rgba(std::uint8_t _r, std::uint8_t _g, std::uint8_t _b, std::uint8_t _a = 255) noexcept
{
  return static_cast<std::uint32_t>(_r) | (static_cast<std::uint32_t>(_g) << 8) | (static_cast<std::uint32_t>(_b) << 16) |
         (static_cast<std::uint32_t>(_a) << 24);
}

/// The same colour with its alpha multiplied by _alpha (0..1).
[[nodiscard]] std::uint32_t FadeColor(std::uint32_t _color, float _alpha) noexcept;

enum class TextAlign : std::uint8_t
{
  Left,
  Center,
  Right
};

/// Builds HUD triangles in pixel coordinates for one frame. Two layers: the back layer is drawn
/// before anything the HUD hosts (the voxel thumbnails), the front layer after it. Shapes with
/// slanted edges get a one-pixel alpha fringe, so they are antialiased without multisampling.
class HudBatch
{
public:
  static constexpr std::size_t LAYER_COUNT = 2;
  static constexpr std::size_t BACK = 0;
  static constexpr std::size_t FRONT = 1;

  void Begin(const FontAtlas* _atlas) noexcept;
  void SetLayer(std::size_t _layer) noexcept
  {
    m_layer = _layer < LAYER_COUNT ? _layer : FRONT;
  }

  void Rect(float _x, float _y, float _width, float _height, std::uint32_t _color);
  void RectGradient(float _x, float _y, float _width, float _height, std::uint32_t _top, std::uint32_t _bottom);
  void Outline(float _x, float _y, float _width, float _height, float _thickness, std::uint32_t _color);
  void Line(float _x0, float _y0, float _x1, float _y1, float _width, std::uint32_t _color);
  void Triangle(DirectX::XMFLOAT2 _a, DirectX::XMFLOAT2 _b, DirectX::XMFLOAT2 _c, std::uint32_t _color);

  /// A filled convex polygon with an antialiased rim.
  void Polygon(std::span<const DirectX::XMFLOAT2> _points, std::uint32_t _color);
  void PolyLine(std::span<const DirectX::XMFLOAT2> _points, bool _closed, float _width, std::uint32_t _color);
  void Circle(float _centerX, float _centerY, float _radius, float _width, std::uint32_t _color, int _segments = 64);
  void Disc(float _centerX, float _centerY, float _radius, std::uint32_t _color, int _segments = 32);

  /// Draws a line of text whose top edge is at _y. Returns its width in pixels.
  float Text(std::size_t _face, float _x, float _y, std::string_view _text, std::uint32_t _color, TextAlign _align = TextAlign::Left);
  [[nodiscard]] float TextWidth(std::size_t _face, std::string_view _text) const noexcept;
  [[nodiscard]] float LineHeight(std::size_t _face) const noexcept;

  [[nodiscard]] std::span<const GpuHudVertex> Vertices(std::size_t _layer) const noexcept
  {
    return m_vertices[_layer];
  }

private:
  void Push(float _x, float _y, float _u, float _v, std::uint32_t _color);
  void PushSolid(float _x, float _y, std::uint32_t _color)
  {
    Push(_x, _y, m_whiteU, m_whiteV, _color);
  }

  const FontAtlas* m_atlas = nullptr;
  std::array<std::vector<GpuHudVertex>, LAYER_COUNT> m_vertices;
  std::size_t m_layer = FRONT;
  float m_whiteU = 0.0f;
  float m_whiteV = 0.0f;
};

} // namespace Neuron
