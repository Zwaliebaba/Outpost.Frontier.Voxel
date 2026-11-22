#pragma once

#include "FontAtlas.h"
#include "GpuTypes.h"
#include "SpriteSheet.h"

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

inline constexpr std::uint32_t WHITE = Rgba(255, 255, 255);

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
///
/// Text and solid shapes sample the font atlas; sprites sample their own atlas (ADR-023). Every
/// vertex says which, so a layer is one draw and its triangles land in exactly the order they
/// were added, whatever mix of text, shapes and sprites they are.
class HudBatch
{
public:
  static constexpr std::size_t LAYER_COUNT = 2;
  static constexpr std::size_t BACK = 0;
  static constexpr std::size_t FRONT = 1;

  /// Starts a frame. _sprites is the sprite manifest FindSprite looks names up in, or null.
  void Begin(const FontAtlas* _atlas, const SpriteSheet* _sprites = nullptr) noexcept;
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

  /// The sprite manifest passed to Begin, or null.
  [[nodiscard]] const SpriteSheet* Sprites() const noexcept
  {
    return m_sprites;
  }
  /// The sprite of that name in the manifest passed to Begin, or null (also when there is none).
  [[nodiscard]] const Sprite* FindSprite(std::string_view _name) const noexcept;

  /// A sprite stretched over the rectangle. A mask's coverage is filled with _tint; colour art,
  /// decoded from sRGB, is multiplied by it, so WHITE draws the art as authored.
  void Image(const Sprite& _sprite, float _x, float _y, float _width, float _height, std::uint32_t _tint = WHITE);

  /// A nine-slice panel over the rectangle: four corners at the sprite's native size times _scale
  /// (pixels per texel), four edges stretched along their length, the centre stretched both ways.
  /// When the rectangle is smaller than two corners, the corners shrink in proportion. Quads of
  /// zero area are skipped; a sprite without slices is drawn as Image. Edges are pixel-snapped.
  void NineSlice(const Sprite& _sprite, float _x, float _y, float _width, float _height, std::uint32_t _tint = WHITE, float _scale = 1.0f);

  [[nodiscard]] std::span<const GpuHudVertex> Vertices(std::size_t _layer) const noexcept
  {
    return m_vertices[_layer];
  }

private:
  void Push(float _x, float _y, float _u, float _v, std::uint32_t _color, std::uint32_t _sampling = 0);
  void PushSolid(float _x, float _y, std::uint32_t _color)
  {
    Push(_x, _y, m_whiteU, m_whiteV, _color);
  }
  void PushQuad(float _x0, float _y0, float _x1, float _y1, float _u0, float _v0, float _u1, float _v1, std::uint32_t _color,
                std::uint32_t _sampling);

  const FontAtlas* m_atlas = nullptr;
  const SpriteSheet* m_sprites = nullptr;
  std::array<std::vector<GpuHudVertex>, LAYER_COUNT> m_vertices;
  std::size_t m_layer = FRONT;
  float m_whiteU = 0.0f;
  float m_whiteV = 0.0f;
};

} // namespace Neuron
