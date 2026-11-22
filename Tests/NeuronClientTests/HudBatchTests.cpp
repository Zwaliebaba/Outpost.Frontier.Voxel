#include "FontAtlas.h"
#include "GpuTypes.h"
#include "HudBatch.h"
#include "SpriteSheet.h"

#include <algorithm>
#include <span>
#include <string>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

/// Vertices in one quad: two triangles.
constexpr std::size_t QUAD = 6;

/// Two atlases: Masks is 256 x 128 coverage in slot 1, Frames 64 x 64 colour art in slot 2.
[[nodiscard]] Neuron::SpriteSheet TestSheet()
{
  const char* text = "atlas name=Masks file=Masks.dds kind=mask\n"
                     "atlas name=Frames file=Frames.dds kind=color\n"
                     "sprite name=Icon atlas=Masks rect=8,16,32,24\n"
                     "sprite name=Panel atlas=Frames rect=16,8,32,32 slice=8,8,8,8\n";
  auto sizes = [](const Neuron::SpriteAtlas& _atlas, std::uint32_t& _outWidth, std::uint32_t& _outHeight, std::string&)
  {
    _outWidth = _atlas.name == "Masks" ? 256 : 64;
    _outHeight = _atlas.name == "Masks" ? 128 : 64;
    return true;
  };
  Neuron::SpriteSheet sheet;
  std::string error;
  Assert::IsTrue(Neuron::SpriteSheet::Parse(text, "Test.txt", sizes, sheet, error));
  return sheet;
}

struct Bounds
{
  float left = 1e9f;
  float top = 1e9f;
  float right = -1e9f;
  float bottom = -1e9f;
};

[[nodiscard]] Bounds BoundsOf(std::span<const Neuron::GpuHudVertex> _vertices)
{
  Bounds bounds;
  for (const Neuron::GpuHudVertex& vertex : _vertices)
  {
    bounds.left = std::min(bounds.left, vertex.position.x);
    bounds.top = std::min(bounds.top, vertex.position.y);
    bounds.right = std::max(bounds.right, vertex.position.x);
    bounds.bottom = std::max(bounds.bottom, vertex.position.y);
  }
  return bounds;
}

/// The vertex of _vertices at a position, which every quad corner of a sprite has exactly.
[[nodiscard]] const Neuron::GpuHudVertex* At(std::span<const Neuron::GpuHudVertex> _vertices, float _x, float _y)
{
  for (const Neuron::GpuHudVertex& vertex : _vertices)
  {
    if (vertex.position.x == _x && vertex.position.y == _y)
    {
      return &vertex;
    }
  }
  return nullptr;
}

} // namespace

TEST_CLASS(HudBatchTests)
{
public:
  TEST_METHOD(ASpriteCoversItsRectWithItsAtlasUvs)
  {
    const Neuron::SpriteSheet sheet = TestSheet();
    Neuron::HudBatch batch;
    batch.Begin(nullptr, &sheet);
    const Neuron::Sprite* icon = batch.FindSprite("Icon");
    Assert::IsNotNull(icon);
    const std::uint32_t tint = Neuron::Rgba(255, 172, 44);
    batch.Image(*icon, 10.0f, 20.0f, 64.0f, 48.0f, tint);

    const auto vertices = batch.Vertices(Neuron::HudBatch::BACK);
    Assert::AreEqual(std::size_t{6}, vertices.size(), L"two triangles");
    const Bounds bounds = BoundsOf(vertices);
    Assert::AreEqual(10.0f, bounds.left);
    Assert::AreEqual(20.0f, bounds.top);
    Assert::AreEqual(74.0f, bounds.right);
    Assert::AreEqual(68.0f, bounds.bottom);
    const Neuron::GpuHudVertex* topLeft = At(vertices, 10.0f, 20.0f);
    const Neuron::GpuHudVertex* bottomRight = At(vertices, 74.0f, 68.0f);
    Assert::IsNotNull(topLeft);
    Assert::IsNotNull(bottomRight);
    Assert::AreEqual(8.0f / 256.0f, topLeft->uv.x);
    Assert::AreEqual(16.0f / 128.0f, topLeft->uv.y);
    Assert::AreEqual(40.0f / 256.0f, bottomRight->uv.x);
    Assert::AreEqual(40.0f / 128.0f, bottomRight->uv.y);
    for (const Neuron::GpuHudVertex& vertex : vertices)
    {
      Assert::AreEqual(tint, vertex.color);
      Assert::AreEqual(1u, vertex.sampling, L"a mask samples slot 1 as coverage");
    }

    batch.Image(*batch.FindSprite("Panel"), 0.0f, 0.0f, 32.0f, 32.0f);
    Assert::AreEqual(2u | Neuron::HUD_SAMPLE_COLOR, batch.Vertices(Neuron::HudBatch::BACK)[6].sampling, L"colour art samples RGBA");
    Assert::AreEqual(Neuron::WHITE, batch.Vertices(Neuron::HudBatch::BACK)[6].color, L"untinted by default");
  }

  TEST_METHOD(ANineSliceIsNineQuadsWithCornersAtNativeSize)
  {
    const Neuron::SpriteSheet sheet = TestSheet();
    const Neuron::Sprite& panel = *sheet.Find("Panel");
    Neuron::HudBatch batch;
    batch.Begin(nullptr, &sheet);
    batch.NineSlice(panel, 100.0f, 50.0f, 300.0f, 120.0f);
    const auto vertices = batch.Vertices(Neuron::HudBatch::BACK);
    Assert::AreEqual(9 * QUAD, vertices.size(), L"nine quads");

    // The corners keep their 8 texels however far the panel stretches; their UVs are the sprite's.
    const Bounds topLeft = BoundsOf(vertices.subspan(0, 6));
    Assert::AreEqual(100.0f, topLeft.left);
    Assert::AreEqual(50.0f, topLeft.top);
    Assert::AreEqual(108.0f, topLeft.right);
    Assert::AreEqual(58.0f, topLeft.bottom);
    const Bounds bottomRight = BoundsOf(vertices.subspan(8 * QUAD, QUAD));
    Assert::AreEqual(392.0f, bottomRight.left);
    Assert::AreEqual(162.0f, bottomRight.top);
    Assert::AreEqual(400.0f, bottomRight.right);
    Assert::AreEqual(170.0f, bottomRight.bottom);
    const Neuron::GpuHudVertex* corner = At(vertices, 108.0f, 58.0f);
    Assert::IsNotNull(corner);
    Assert::AreEqual((16.0f + 8.0f) / 64.0f, corner->uv.x, L"the slice line in texture space");
    Assert::AreEqual((8.0f + 8.0f) / 64.0f, corner->uv.y);
    const Bounds center = BoundsOf(vertices.subspan(4 * QUAD, QUAD));
    Assert::AreEqual(108.0f, center.left);
    Assert::AreEqual(392.0f, center.right, L"the centre takes the stretch");

    // At twice the scale the corners are 16 pixels; in a rect too small for them they shrink.
    batch.Begin(nullptr, &sheet);
    batch.NineSlice(panel, 0.0f, 0.0f, 200.0f, 100.0f, Neuron::WHITE, 2.0f);
    const Bounds scaled = BoundsOf(batch.Vertices(Neuron::HudBatch::BACK).subspan(0, 6));
    Assert::AreEqual(16.0f, scaled.right);
    Assert::AreEqual(16.0f, scaled.bottom);
    batch.Begin(nullptr, &sheet);
    batch.NineSlice(panel, 0.0f, 0.0f, 10.0f, 10.0f);
    const auto small = batch.Vertices(Neuron::HudBatch::BACK);
    Assert::AreEqual(4 * QUAD, small.size(), L"no centre or edges are left, only four corners");
    Assert::AreEqual(5.0f, BoundsOf(small.subspan(0, 6)).right);
    Assert::AreEqual(10.0f, BoundsOf(small).right);
  }

  TEST_METHOD(TextShapesAndSpritesKeepTheirOrderAndLayer)
  {
    Neuron::FontAtlas fonts;
    Assert::IsTrue(fonts.Build({Neuron::FontSpec{L"Segoe UI", 16, 400}}), L"the GDI font atlas builds");
    const Neuron::SpriteSheet sheet = TestSheet();
    Neuron::HudBatch batch;
    batch.Begin(&fonts, &sheet);

    // Back layer, under the thumbnails the client draws between the layers: a fill, a panel,
    // text on the panel, then an icon over the text.
    batch.Rect(0.0f, 0.0f, 50.0f, 50.0f, Neuron::Rgba(7, 17, 25));
    batch.NineSlice(*sheet.Find("Panel"), 0.0f, 0.0f, 100.0f, 40.0f);
    batch.Text(0, 4.0f, 4.0f, "AB", Neuron::WHITE);
    batch.Image(*sheet.Find("Icon"), 60.0f, 4.0f, 16.0f, 16.0f);
    // Front layer, over the thumbnails.
    batch.SetLayer(Neuron::HudBatch::FRONT);
    batch.Image(*sheet.Find("Icon"), 200.0f, 4.0f, 16.0f, 16.0f);
    batch.Text(0, 220.0f, 4.0f, "C", Neuron::WHITE);

    const auto back = batch.Vertices(Neuron::HudBatch::BACK);
    Assert::AreEqual(std::size_t{6 + 54 + 12 + 6}, back.size());
    auto range = [&back](std::size_t _first, std::size_t _count, std::uint32_t _sampling)
    {
      for (std::size_t i = _first; i < _first + _count; ++i)
      {
        Assert::AreEqual(_sampling, back[i].sampling);
      }
    };
    range(0, 6, 0u);                             // the fill, from the font atlas's white block
    range(6, 54, 2u | Neuron::HUD_SAMPLE_COLOR); // the panel
    range(60, 12, 0u);                           // two glyphs
    range(72, 6, 1u);                            // the icon, last, so on top
    const auto front = batch.Vertices(Neuron::HudBatch::FRONT);
    Assert::AreEqual(std::size_t{12}, front.size());
    Assert::AreEqual(1u, front[0].sampling);
    Assert::AreEqual(0u, front[6].sampling);
    Assert::AreEqual(200.0f, BoundsOf(front.subspan(0, 6)).left, L"front-layer sprites stay out of the back layer");

    batch.Begin(&fonts, &sheet);
    Assert::IsTrue(batch.Vertices(Neuron::HudBatch::BACK).empty() && batch.Vertices(Neuron::HudBatch::FRONT).empty());
    batch.Image(*sheet.Find("Icon"), 0.0f, 0.0f, 8.0f, 8.0f);
    Assert::AreEqual(std::size_t{6}, batch.Vertices(Neuron::HudBatch::BACK).size(), L"Begin starts on the back layer");
  }

  TEST_METHOD(WithoutAManifestNoSpriteIsFound)
  {
    Neuron::HudBatch batch;
    batch.Begin(nullptr);
    Assert::IsNull(batch.FindSprite("Panel.Frame"));
    Assert::IsNull(batch.Sprites());
  }
};

} // namespace NeuronClientTests
