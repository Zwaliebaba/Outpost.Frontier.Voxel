#include "FileSys.h"
#include "FontAtlas.h"
#include "HudBatch.h"
#include "SpriteSheet.h"
#include "TacticalHud.h"

#include "CppUnitTest.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameAppTests
{

namespace
{

/// Line thickness may put a vertex a pixel past an edge it is drawn along.
constexpr float EDGE_TOLERANCE_PIXELS = 2.0f;

int g_moduleAnchor = 0;

/// The shipped sprite manifest and its atlases (ADR-023), found above the test DLL.
[[nodiscard]] Neuron::SpriteSheet ShippedSprites()
{
  const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
  Assert::IsTrue(root.has_value(), L"Assets/Scene.txt is found above the test DLL");
  Neuron::SpriteSheet sheet;
  std::vector<Neuron::DdsImage> images;
  std::string error;
  Assert::IsTrue(Neuron::LoadSpriteSheet(*root / "Ui" / "Sprites.txt", sheet, images, error),
                 std::wstring(error.begin(), error.end()).c_str());
  return sheet;
}

[[nodiscard]] Outpost::WorldSnapshot Snapshot()
{
  Outpost::WorldSnapshot snapshot;
  snapshot.ships.push_back(Outpost::ShipState{
    "Frigate", "F-317", "Frigate", 68, {{"Hull Section (B)", 34}, {"Engineering", 72}, {"Atmospherics", 75}, {"Bridge", 100}}});
  snapshot.objectives.push_back(Outpost::ObjectiveState{"Defend the shipyard", true});
  snapshot.objectives.push_back(Outpost::ObjectiveState{"Escort tug T-041 to dock", false});
  snapshot.resources.push_back(Outpost::ResourceState{"Alloy", 12400, 0});
  snapshot.resources.push_back(Outpost::ResourceState{"Crew", 36, 60});
  return snapshot;
}

[[nodiscard]] Neuron::ProjectedBounds Bounds(Neuron::ScreenVisibility _visibility, Neuron::ScreenRect _rect, DirectX::XMFLOAT2 _edge)
{
  Neuron::ProjectedBounds bounds;
  bounds.visibility = _visibility;
  bounds.rect = _rect;
  bounds.edgeDirection = _edge;
  return bounds;
}

struct BuiltHud
{
  Neuron::FontAtlas atlas;
  Neuron::HudBatch batch;
  Outpost::HudLayout layout;
};

/// Builds the HUD at a window size, with the scale the client uses for it, and ships on screen,
/// off screen and behind the camera; skinned from _sprites when there are any.
void BuildHud(float _width, float _height, const Outpost::WorldSnapshot& _snapshot, BuiltHud& _out,
              const Neuron::SpriteSheet* _sprites = nullptr)
{
  const float scale = std::clamp(_height / 1080.0f, 0.6f, 2.0f);
  Assert::IsTrue(_out.atlas.Build(Outpost::TacticalHud::Fonts(scale)), L"the GDI font atlas builds");
  _out.batch.Begin(&_out.atlas, _sprites);
  Outpost::HudInput input;
  input.width = _width;
  input.height = _height;
  input.scale = scale;
  input.snapshot = &_snapshot;
  input.selected = &_snapshot.ships[0];
  input.buildLabels = {"HANGAR", "COMMAND", "TANKS", "TRUSS"};
  input.statusLine = "status";
  const float w = _width;
  const float h = _height;
  using Neuron::ScreenVisibility;
  input.markers = {
    {"F-317", Bounds(ScreenVisibility::OnScreen, {w * 0.4f, h * 0.4f, w * 0.6f, h * 0.6f}, {0, -1}), 68, true},
    {"E-1", Bounds(ScreenVisibility::OnScreen, {-40.0f, h * 0.5f, 30.0f, h * 0.55f}, {-1, 0}), 90, false},
    {"E-2", Bounds(ScreenVisibility::OffScreen, {}, {1.0f, 0.0f}), 100, false},
    {"T-041", Bounds(ScreenVisibility::OffScreen, {}, {0.0f, 1.0f}), 55, false},
    {"X-9", Bounds(ScreenVisibility::BehindCamera, {}, {-0.70710678f, -0.70710678f}), 20, false},
  };
  _out.layout = Outpost::TacticalHud{}.Build(_out.batch, input);
}

[[nodiscard]] bool Inside(const Neuron::ScreenRect& _rect, float _width, float _height)
{
  return _rect.left >= 0.0f && _rect.top >= 0.0f && _rect.right <= _width && _rect.bottom <= _height && _rect.right > _rect.left &&
         _rect.bottom > _rect.top;
}

} // namespace

TEST_CLASS(TacticalHudTests)
{
public:
  TEST_METHOD(EveryVertexStaysOnScreenAtEveryWindowSize)
  {
    const Outpost::WorldSnapshot snapshot = Snapshot();
    for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, std::pair{1920.0f, 1080.0f}, std::pair{3840.0f, 2160.0f}})
    {
      BuiltHud hud;
      BuildHud(width, height, snapshot, hud);
      std::size_t vertices = 0;
      for (std::size_t layer = 0; layer < Neuron::HudBatch::LAYER_COUNT; ++layer)
      {
        for (const Neuron::GpuHudVertex& vertex : hud.batch.Vertices(layer))
        {
          ++vertices;
          const bool inside = vertex.position.x >= -EDGE_TOLERANCE_PIXELS && vertex.position.x <= width + EDGE_TOLERANCE_PIXELS &&
                              vertex.position.y >= -EDGE_TOLERANCE_PIXELS && vertex.position.y <= height + EDGE_TOLERANCE_PIXELS;
          if (!inside)
          {
            const std::wstring message = L"vertex at " + std::to_wstring(vertex.position.x) + L", " + std::to_wstring(vertex.position.y) +
                                         L" outside " + std::to_wstring(width) + L"x" + std::to_wstring(height);
            Assert::Fail(message.c_str());
          }
        }
      }
      Assert::IsTrue(vertices > 1000, L"the HUD drew something");
    }
  }

  TEST_METHOD(TheBuildPanelHasOneOnScreenSlotPerItem)
  {
    const Outpost::WorldSnapshot snapshot = Snapshot();
    BuiltHud hud;
    BuildHud(1920.0f, 1080.0f, snapshot, hud);
    Assert::AreEqual(std::size_t{4}, hud.layout.buildSlots.size());
    for (std::size_t i = 0; i < hud.layout.buildSlots.size(); ++i)
    {
      Assert::IsTrue(Inside(hud.layout.buildSlots[i], 1920.0f, 1080.0f));
      if (i > 0)
      {
        Assert::IsTrue(hud.layout.buildSlots[i].left >= hud.layout.buildSlots[i - 1].right, L"slots do not overlap");
      }
    }
    Assert::IsTrue(Inside(hud.layout.silhouette, 1920.0f, 1080.0f), L"the selected ship's silhouette has a place");
  }

  TEST_METHOD(TheLayoutScalesWithTheWindow)
  {
    const Outpost::WorldSnapshot snapshot = Snapshot();
    BuiltHud small;
    BuiltHud large;
    BuildHud(1920.0f, 1080.0f, snapshot, small);
    BuildHud(3840.0f, 2160.0f, snapshot, large);
    Assert::AreEqual(small.layout.buildSlots.size(), large.layout.buildSlots.size());
    for (std::size_t i = 0; i < small.layout.buildSlots.size(); ++i)
    {
      const Neuron::ScreenRect& a = small.layout.buildSlots[i];
      const Neuron::ScreenRect& b = large.layout.buildSlots[i];
      Assert::IsTrue(std::abs((b.right - b.left) - 2.0f * (a.right - a.left)) <= 2.0f, L"twice the window, twice the slot");
      Assert::IsTrue(std::abs(b.left - 2.0f * a.left) <= 2.0f);
    }
  }

  TEST_METHOD(TheSkinnedHudKeepsItsLayoutAndStaysOnScreen)
  {
    const Outpost::WorldSnapshot snapshot = Snapshot();
    const Neuron::SpriteSheet sprites = ShippedSprites();
    for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, std::pair{1920.0f, 1080.0f}, std::pair{3840.0f, 2160.0f}})
    {
      BuiltHud plain;
      BuiltHud skinned;
      BuildHud(width, height, snapshot, plain);
      BuildHud(width, height, snapshot, skinned, &sprites);

      // The re-skin changes how panels look, never where anything is.
      Assert::AreEqual(plain.layout.buildSlots.size(), skinned.layout.buildSlots.size());
      for (std::size_t i = 0; i < plain.layout.buildSlots.size(); ++i)
      {
        const Neuron::ScreenRect& a = plain.layout.buildSlots[i];
        const Neuron::ScreenRect& b = skinned.layout.buildSlots[i];
        Assert::IsTrue(a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom, L"the same build slots");
      }
      const Neuron::ScreenRect& a = plain.layout.silhouette;
      const Neuron::ScreenRect& b = skinned.layout.silhouette;
      Assert::IsTrue(a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom, L"the same silhouette");

      std::size_t spriteVertices = 0;
      std::size_t colorVertices = 0;
      for (std::size_t layer = 0; layer < Neuron::HudBatch::LAYER_COUNT; ++layer)
      {
        for (const Neuron::GpuHudVertex& vertex : skinned.batch.Vertices(layer))
        {
          Assert::IsTrue(vertex.position.x >= -EDGE_TOLERANCE_PIXELS && vertex.position.x <= width + EDGE_TOLERANCE_PIXELS &&
                           vertex.position.y >= -EDGE_TOLERANCE_PIXELS && vertex.position.y <= height + EDGE_TOLERANCE_PIXELS,
                         L"every skinned vertex stays on screen");
          spriteVertices += (vertex.sampling & Neuron::HUD_SLOT_MASK) != 0 ? 1 : 0;
          colorVertices += (vertex.sampling & Neuron::HUD_SAMPLE_COLOR) != 0 ? 1 : 0;
        }
      }
      // Seven panels of nine quads, five buttons and four build slots at the least.
      Assert::IsTrue(spriteVertices >= std::size_t{7 + 5 + 4} * 9 * 6, L"panels, buttons and slots are drawn from sprites");
      Assert::IsTrue(colorVertices > 0 && colorVertices < spriteVertices, L"both colour frames and tinted masks");
    }
  }

  TEST_METHOD(FontsScaleAndKeepAMinimumSize)
  {
    const std::vector<Neuron::FontSpec> normal = Outpost::TacticalHud::Fonts(1.0f);
    const std::vector<Neuron::FontSpec> doubled = Outpost::TacticalHud::Fonts(2.0f);
    const std::vector<Neuron::FontSpec> tiny = Outpost::TacticalHud::Fonts(0.1f);
    Assert::AreEqual(static_cast<std::size_t>(Outpost::HudFontCount), normal.size());
    for (std::size_t i = 0; i < normal.size(); ++i)
    {
      Assert::AreEqual(normal[i].pixelHeight * 2, doubled[i].pixelHeight);
      Assert::IsTrue(tiny[i].pixelHeight >= 8, L"text stays legible");
    }
  }
};

} // namespace GameAppTests
