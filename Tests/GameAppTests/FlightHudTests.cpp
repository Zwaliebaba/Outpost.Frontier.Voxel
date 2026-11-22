#include "FlightHud.h"
#include "FontAtlas.h"
#include "HudBatch.h"
#include "TacticalHud.h"

#include "CppUnitTest.h"

#include <algorithm>
#include <string>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameAppTests
{

namespace
{

constexpr float EDGE_TOLERANCE_PIXELS = 2.0f;

[[nodiscard]] bool Inside(const Neuron::ScreenRect& _rect, float _width, float _height)
{
  return _rect.left >= 0.0f && _rect.top >= 0.0f && _rect.right <= _width && _rect.bottom <= _height && _rect.right > _rect.left &&
         _rect.bottom > _rect.top;
}

[[nodiscard]] bool Overlap(const Neuron::ScreenRect& _a, const Neuron::ScreenRect& _b)
{
  return _a.left < _b.right && _b.left < _a.right && _a.top < _b.bottom && _b.top < _a.bottom;
}

/// Every part of the HUD at once: steering by the locked cursor at a corner, the thruster, cruise
/// charging, the engine killed and an order.
[[nodiscard]] Outpost::FlightHudInput Busy(float _width, float _height, float _scale)
{
  Outpost::FlightHudInput input;
  input.width = _width;
  input.height = _height;
  input.scale = _scale;
  input.callsign = "E-208";
  input.speedMetresPerSecond = 213.0f;
  input.throttle = 0.7f;
  input.powerShare = 0.4f;
  input.thrustShare = 0.1f;
  input.cruise = Outpost::CruiseState::Charging;
  input.cruiseChargeShare = 0.6f;
  input.engineOn = false;
  input.thruster = true;
  input.mouseSteering = true;
  input.mouseLocked = true;
  input.cursor = DirectX::XMFLOAT2{_width, _height};
  input.order = "GO TO  1250 M";
  input.statusLine = "status";
  return input;
}

} // namespace

TEST_CLASS(FlightHudTests)
{
public:
  TEST_METHOD(EveryPartStaysOnScreenAtEveryWindowSize)
  {
    for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, std::pair{1920.0f, 1080.0f}, std::pair{3840.0f, 2160.0f}})
    {
      const float scale = std::clamp(height / 1080.0f, 0.6f, 2.0f);
      Neuron::FontAtlas atlas;
      Assert::IsTrue(atlas.Build(Outpost::TacticalHud::Fonts(scale)));
      Neuron::HudBatch batch;
      batch.Begin(&atlas);
      const Outpost::FlightHudLayout layout = Outpost::FlightHud{}.Build(batch, Busy(width, height, scale));
      for (const Neuron::ScreenRect& rect : {layout.panel, layout.throttleBar, layout.powerBar, layout.thrustBar, layout.help})
      {
        Assert::IsTrue(Inside(rect, width, height), (L"a part leaves the screen at " + std::to_wstring(width)).c_str());
      }
      Assert::IsFalse(Overlap(layout.panel, layout.help), L"the help line is clear of the panel");
      Assert::IsFalse(Overlap(layout.powerBar, layout.thrustBar));
      std::size_t vertices = 0;
      for (std::size_t layer = 0; layer < Neuron::HudBatch::LAYER_COUNT; ++layer)
      {
        for (const Neuron::GpuHudVertex& vertex : batch.Vertices(layer))
        {
          ++vertices;
          Assert::IsTrue(vertex.position.x >= -EDGE_TOLERANCE_PIXELS && vertex.position.x <= width + EDGE_TOLERANCE_PIXELS &&
                           vertex.position.y >= -EDGE_TOLERANCE_PIXELS && vertex.position.y <= height + EDGE_TOLERANCE_PIXELS,
                         L"every vertex is on screen, the cursor at the corner included");
        }
      }
      Assert::IsTrue(vertices > 500, L"the HUD drew something");
    }
  }

  TEST_METHOD(ThePanelScalesWithTheWindow)
  {
    Neuron::FontAtlas small;
    Neuron::FontAtlas large;
    Assert::IsTrue(small.Build(Outpost::TacticalHud::Fonts(1.0f)));
    Assert::IsTrue(large.Build(Outpost::TacticalHud::Fonts(2.0f)));
    Neuron::HudBatch batch;
    batch.Begin(&small);
    const Outpost::FlightHudLayout at1080 = Outpost::FlightHud{}.Build(batch, Busy(1920.0f, 1080.0f, 1.0f));
    batch.Begin(&large);
    const Outpost::FlightHudLayout at2160 = Outpost::FlightHud{}.Build(batch, Busy(3840.0f, 2160.0f, 2.0f));
    const float width1080 = at1080.panel.right - at1080.panel.left;
    const float width2160 = at2160.panel.right - at2160.panel.left;
    Assert::AreEqual(2.0f * width1080, width2160, 0.5f, L"twice the lines, twice the panel");
  }
};

} // namespace GameAppTests
