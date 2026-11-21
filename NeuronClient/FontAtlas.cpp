#include "FontAtlas.h"

#include "WindowsSdk.h"

#include <algorithm>
#include <bit>
#include <cwchar>

namespace Neuron
{

namespace
{

constexpr int ATLAS_WIDTH = 1024;
constexpr int GLYPH_PADDING = 2;

struct GdiFont
{
  HFONT font = nullptr;
  GdiFont() = default;
  GdiFont(const GdiFont&) = delete;
  GdiFont& operator=(const GdiFont&) = delete;
  ~GdiFont()
  {
    if (font != nullptr)
    {
      DeleteObject(font);
    }
  }
};

[[nodiscard]] HFONT CreateUiFont(const std::wstring& _face, int _pixelHeight, int _weight) noexcept
{
  return CreateFontW(-_pixelHeight, 0, 0, 0, _weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                     ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, _face.c_str());
}

} // namespace

bool FontAtlas::Build(const std::vector<FontSpec>& _fonts)
{
  m_faces.clear();
  HDC dc = CreateCompatibleDC(nullptr);
  if (dc == nullptr)
  {
    return false;
  }

  // First pass: resolve fonts and lay glyph cells out in rows.
  struct Cell
  {
    std::size_t face;
    int glyph;
    int x;
    int y;
    int width;
    int drawX;
  };
  std::vector<Cell> cells;
  std::vector<GdiFont> fonts(_fonts.size());
  int penX = 4 + GLYPH_PADDING; // leave the white block alone
  int penY = 0;
  int rowHeight = 4 + GLYPH_PADDING;
  for (std::size_t f = 0; f < _fonts.size(); ++f)
  {
    fonts[f].font = CreateUiFont(_fonts[f].face, _fonts[f].pixelHeight, _fonts[f].weight);
    HGDIOBJ previous = SelectObject(dc, fonts[f].font);
    wchar_t resolved[LF_FACESIZE]{};
    GetTextFaceW(dc, LF_FACESIZE, resolved);
    if (_wcsicmp(resolved, _fonts[f].face.c_str()) != 0)
    {
      SelectObject(dc, previous);
      DeleteObject(fonts[f].font);
      fonts[f].font = CreateUiFont(L"Segoe UI", _fonts[f].pixelHeight, _fonts[f].weight);
      SelectObject(dc, fonts[f].font);
      GetTextFaceW(dc, LF_FACESIZE, resolved);
    }
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    FontFace face;
    face.lineHeight = metrics.tmHeight;
    face.ascent = metrics.tmAscent;
    face.resolvedFace = resolved;
    for (int g = 0; g < static_cast<int>(GLYPH_COUNT); ++g)
    {
      const wchar_t character = static_cast<wchar_t>(FIRST_GLYPH + g);
      ABC abc{};
      if (!GetCharABCWidthsW(dc, character, character, &abc))
      {
        SIZE size{};
        GetTextExtentPoint32W(dc, &character, 1, &size);
        abc.abcA = 0;
        abc.abcB = static_cast<UINT>(size.cx);
        abc.abcC = 0;
      }
      const int left = std::min(abc.abcA, 0);
      const int width = static_cast<int>(abc.abcB) + std::max(abc.abcA, 0) - left + std::max(-abc.abcC, 0) + 2;
      if (penX + width + GLYPH_PADDING > ATLAS_WIDTH)
      {
        penX = GLYPH_PADDING;
        penY += rowHeight + GLYPH_PADDING;
        rowHeight = 0;
      }
      Glyph& glyph = face.glyphs[static_cast<std::size_t>(g)];
      glyph.width = width;
      glyph.height = metrics.tmHeight;
      glyph.offsetX = left - 1;
      glyph.advance = abc.abcA + static_cast<int>(abc.abcB) + abc.abcC;
      cells.push_back(Cell{f, g, penX, penY, width, penX - left + 1});
      penX += width + GLYPH_PADDING;
      rowHeight = std::max(rowHeight, static_cast<int>(metrics.tmHeight));
    }
    m_faces.push_back(std::move(face));
    SelectObject(dc, previous);
  }
  const int atlasHeight = std::max(64, static_cast<int>(std::bit_ceil(static_cast<unsigned>(penY + rowHeight + GLYPH_PADDING))));

  // Second pass: draw white text on black into a top-down 32-bit DIB.
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = ATLAS_WIDTH;
  info.bmiHeader.biHeight = -atlasHeight;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bitmap == nullptr || bits == nullptr)
  {
    DeleteDC(dc);
    return false;
  }
  HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
  std::fill_n(static_cast<std::uint32_t*>(bits), static_cast<std::size_t>(ATLAS_WIDTH) * atlasHeight, 0u);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  SetTextAlign(dc, TA_TOP | TA_LEFT);
  for (const Cell& cell : cells)
  {
    HGDIOBJ previous = SelectObject(dc, fonts[cell.face].font);
    const wchar_t character = static_cast<wchar_t>(FIRST_GLYPH + cell.glyph);
    TextOutW(dc, cell.drawX, cell.y, &character, 1);
    SelectObject(dc, previous);
  }
  GdiFlush();

  m_width = ATLAS_WIDTH;
  m_height = static_cast<std::uint32_t>(atlasHeight);
  m_coverage.assign(static_cast<std::size_t>(m_width) * m_height, 0);
  const auto* pixels = static_cast<const std::uint32_t*>(bits);
  for (std::size_t i = 0; i < m_coverage.size(); ++i)
  {
    m_coverage[i] = static_cast<std::uint8_t>((pixels[i] >> 8) & 0xffu); // green channel
  }
  for (int y = 0; y < 4; ++y)
  {
    for (int x = 0; x < 4; ++x)
    {
      m_coverage[static_cast<std::size_t>(y) * m_width + static_cast<std::size_t>(x)] = 255;
    }
  }
  for (const Cell& cell : cells)
  {
    Glyph& glyph = m_faces[cell.face].glyphs[static_cast<std::size_t>(cell.glyph)];
    glyph.u0 = static_cast<float>(cell.x) / static_cast<float>(m_width);
    glyph.v0 = static_cast<float>(cell.y) / static_cast<float>(m_height);
    glyph.u1 = static_cast<float>(cell.x + glyph.width) / static_cast<float>(m_width);
    glyph.v1 = static_cast<float>(cell.y + glyph.height) / static_cast<float>(m_height);
  }

  SelectObject(dc, previousBitmap);
  DeleteObject(bitmap);
  DeleteDC(dc);
  return true;
}

int FontAtlas::MeasureText(std::size_t _face, std::string_view _text) const noexcept
{
  if (_face >= m_faces.size())
  {
    return 0;
  }
  int width = 0;
  for (const char character : _text)
  {
    if (character >= FIRST_GLYPH && character <= LAST_GLYPH)
    {
      width += m_faces[_face].glyphs[static_cast<std::size_t>(character - FIRST_GLYPH)].advance;
    }
  }
  return width;
}

} // namespace Neuron
