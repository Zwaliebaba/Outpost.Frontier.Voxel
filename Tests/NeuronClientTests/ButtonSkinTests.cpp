#include "ButtonSkin.h"
#include "FontAtlas.h"
#include "HudBatch.h"
#include "SpriteSheet.h"

#include <string>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

/// Vertices in one quad: two triangles.
constexpr std::size_t QUAD = 6;

/// A colour atlas with a 16-texel button frame per control state, and a mask with an icon and a ring.
[[nodiscard]] std::string ButtonManifest(bool _withRefused = true)
{
  std::string text = "atlas name=Frames file=Frames.dds kind=color\natlas name=Masks file=Masks.dds kind=mask\n"
                     "sprite name=Icon.Repair atlas=Masks rect=0,0,16,16\n"
                     "sprite name=Focus.Ring atlas=Masks rect=16,0,16,16 slice=4,4,4,4\n";
  const char* states[] = {"Idle", "Hover", "Pressed", "Selected", "Disabled", "Pending", "Confirmed", "Refused"};
  int x = 0;
  for (const char* state : states)
  {
    if (!_withRefused && std::string(state) == "Refused")
    {
      continue;
    }
    text += "sprite name=Button." + std::string(state) + " atlas=Frames rect=" + std::to_string(x) + ",0,16,16 slice=4,4,4,4\n";
    x += 16;
  }
  return text;
}

[[nodiscard]] Neuron::SpriteSheet Sheet(bool _withRefused = true)
{
  auto sizes = [](const Neuron::SpriteAtlas&, std::uint32_t& _outWidth, std::uint32_t& _outHeight, std::string&)
  {
    _outWidth = 128;
    _outHeight = 16;
    return true;
  };
  Neuron::SpriteSheet sheet;
  std::string error;
  Assert::IsTrue(Neuron::SpriteSheet::Parse(ButtonManifest(_withRefused), "Test.txt", sizes, sheet, error));
  return sheet;
}

[[nodiscard]] Neuron::ButtonStyle Style()
{
  Neuron::ButtonStyle style;
  style.labelFace = 0;
  style.reasonFace = 1;
  return style;
}

} // namespace

TEST_CLASS(ButtonSkinTests)
{
public:
  TEST_METHOD(ResolveFindsAFramePerStateAndNamesAMissingOne)
  {
    const Neuron::SpriteSheet sheet = Sheet();
    Neuron::ButtonSkin skin;
    std::string error;
    Assert::IsTrue(skin.Resolve(sheet, "Button", "Focus.Ring", error));
    Assert::IsTrue(skin.IsResolved());
    Assert::IsTrue(skin.Frame(Neuron::ControlState::Pending) == sheet.Find("Button.Pending"));

    const Neuron::SpriteSheet partial = Sheet(false);
    Neuron::ButtonSkin incomplete;
    Assert::IsFalse(incomplete.Resolve(partial, "Button", {}, error));
    Assert::IsTrue(error.find("Button.Refused") != std::string::npos);
    Assert::IsFalse(incomplete.IsResolved());
    Assert::IsFalse(skin.Resolve(sheet, "Button", "Focus.Missing", error));
  }

  TEST_METHOD(ADisabledButtonAlwaysShowsItsReason)
  {
    Assert::IsTrue(Neuron::ButtonSkin::ShowsReason(Neuron::ControlState::Disabled, false, "Hangar destroyed"));
    Assert::IsTrue(Neuron::ButtonSkin::ShowsReason(Neuron::ControlState::Refused, false, "No engine"));
    Assert::IsFalse(Neuron::ButtonSkin::ShowsReason(Neuron::ControlState::Idle, false, "Costs 40 Alloy"), L"only on hover");
    Assert::IsTrue(Neuron::ButtonSkin::ShowsReason(Neuron::ControlState::Hover, false, "Costs 40 Alloy"));
    Assert::IsTrue(Neuron::ButtonSkin::ShowsReason(Neuron::ControlState::Idle, true, "Costs 40 Alloy"), L"or with the keyboard focus");
    Assert::IsFalse(Neuron::ButtonSkin::ShowsReason(Neuron::ControlState::Disabled, false, {}), L"nothing to show");

    Neuron::FontAtlas fonts;
    Assert::IsTrue(fonts.Build({Neuron::FontSpec{L"Segoe UI", 16, 400}, Neuron::FontSpec{L"Segoe UI", 12, 400}}));
    const Neuron::SpriteSheet sheet = Sheet();
    Neuron::ButtonSkin skin;
    std::string error;
    Assert::IsTrue(skin.Resolve(sheet, "Button", "Focus.Ring", error));
    Neuron::HudBatch batch;

    // Tall enough for both lines: the reason sits under the label, inside the frame.
    batch.Begin(&fonts, &sheet);
    Neuron::ButtonContent content{sheet.Find("Icon.Repair"), "REPAIR", "Hangar destroyed", false};
    Neuron::ButtonLayout layout =
      skin.Draw(batch, Neuron::ScreenRect{10.0f, 10.0f, 210.0f, 70.0f}, Neuron::ControlState::Disabled, content, Style());
    Assert::IsTrue(layout.reasonShown && layout.reasonInside);
    Assert::IsTrue(layout.reasonY > layout.labelY);
    Assert::IsTrue(layout.reasonY + batch.LineHeight(1) <= 70.0f);
    const std::size_t frameAndIcon = 9 * QUAD + QUAD;
    const std::size_t glyphs = (std::string("REPAIR").size() + std::string("Hangardestroyed").size()) * QUAD;
    Assert::AreEqual(frameAndIcon + glyphs, batch.Vertices(Neuron::HudBatch::BACK).size(), L"frame, icon, label and reason");
    Assert::IsTrue(batch.Vertices(Neuron::HudBatch::BACK)[0].sampling == (1u | Neuron::HUD_SAMPLE_COLOR), L"the Disabled frame first");
    Assert::AreEqual(layout.icon.right - layout.icon.left, layout.icon.bottom - layout.icon.top, L"the icon is square");

    // Too short for two lines: the reason goes just below the frame.
    batch.Begin(&fonts, &sheet);
    layout = skin.Draw(batch, Neuron::ScreenRect{10.0f, 10.0f, 210.0f, 34.0f}, Neuron::ControlState::Disabled, content, Style());
    Assert::IsTrue(layout.reasonShown && !layout.reasonInside);
    Assert::IsTrue(layout.reasonY >= 34.0f);

    // An idle button keeps its reason to itself, and an icon-only button centres its icon.
    batch.Begin(&fonts, &sheet);
    layout = skin.Draw(batch, Neuron::ScreenRect{0.0f, 0.0f, 50.0f, 50.0f}, Neuron::ControlState::Idle,
                       Neuron::ButtonContent{sheet.Find("Icon.Repair"), {}, "Costs 40 Alloy", false}, Style());
    Assert::IsFalse(layout.reasonShown);
    Assert::AreEqual(25.0f, (layout.icon.left + layout.icon.right) * 0.5f);
    Assert::AreEqual(9 * QUAD + QUAD, batch.Vertices(Neuron::HudBatch::BACK).size());

    // With the keyboard focus the ring is drawn first, around the frame.
    batch.Begin(&fonts, &sheet);
    static_cast<void>(skin.Draw(batch, Neuron::ScreenRect{20.0f, 20.0f, 70.0f, 70.0f}, Neuron::ControlState::Hover,
                                Neuron::ButtonContent{nullptr, {}, {}, true}, Style()));
    const auto vertices = batch.Vertices(Neuron::HudBatch::BACK);
    Assert::AreEqual(2 * (9 * QUAD), vertices.size(), L"the ring and the frame");
    Assert::AreEqual(2u, vertices[0].sampling, L"the ring is a mask");
    Assert::IsTrue(vertices[0].position.x < 20.0f, L"outside the frame");
  }
};

} // namespace NeuronClientTests
