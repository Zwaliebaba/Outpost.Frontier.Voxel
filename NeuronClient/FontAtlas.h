#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

struct FontSpec
{
  std::wstring face;    // tried first; "Segoe UI" is the fallback when it is not installed
  int pixelHeight = 16; // cell height in pixels
  int weight = 400;     // GDI weight: 400 regular, 600 semibold, 700 bold
};

struct Glyph
{
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
  int width = 0; // bitmap size in pixels
  int height = 0;
  int offsetX = 0; // from the pen position to the bitmap's left edge
  int advance = 0; // pen advance in pixels
};

inline constexpr char FIRST_GLYPH = ' ';
inline constexpr char LAST_GLYPH = '~';
inline constexpr std::size_t GLYPH_COUNT = static_cast<std::size_t>(LAST_GLYPH) - static_cast<std::size_t>(FIRST_GLYPH) + 1;

struct FontFace
{
  int lineHeight = 0;
  int ascent = 0;
  std::wstring resolvedFace;
  std::array<Glyph, GLYPH_COUNT> glyphs{};
};

/// Printable ASCII in a set of fonts, rasterized by GDI into one R8 coverage atlas. The top-left
/// 4x4 texels are solid white so that untextured HUD shapes sample full coverage from the same
/// texture. Rebuilt whenever the HUD scale changes, so text stays pixel-exact at any window size.
class FontAtlas
{
public:
  [[nodiscard]] bool Build(const std::vector<FontSpec>& _fonts);

  [[nodiscard]] std::uint32_t Width() const noexcept
  {
    return m_width;
  }
  [[nodiscard]] std::uint32_t Height() const noexcept
  {
    return m_height;
  }
  [[nodiscard]] const std::vector<std::uint8_t>& Coverage() const noexcept
  {
    return m_coverage;
  }
  [[nodiscard]] std::size_t FaceCount() const noexcept
  {
    return m_faces.size();
  }
  [[nodiscard]] const FontFace& Face(std::size_t _index) const noexcept
  {
    return m_faces[_index];
  }
  [[nodiscard]] float WhiteU() const noexcept
  {
    return 2.0f / static_cast<float>(m_width);
  }
  [[nodiscard]] float WhiteV() const noexcept
  {
    return 2.0f / static_cast<float>(m_height);
  }

  [[nodiscard]] int MeasureText(std::size_t _face, std::string_view _text) const noexcept;

private:
  std::uint32_t m_width = 0;
  std::uint32_t m_height = 0;
  std::vector<std::uint8_t> m_coverage;
  std::vector<FontFace> m_faces;
};

} // namespace Neuron
