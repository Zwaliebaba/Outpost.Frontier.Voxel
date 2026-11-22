#include "FileSys.h"
#include "SpriteSheet.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

int g_moduleAnchor = 0;

/// Sizes for the test manifests: Masks is 256 x 128, Frames (and Frames2, ...) 64 x 64, anything else is unknown.
bool FixedSizes(const Neuron::SpriteAtlas& _atlas, std::uint32_t& _outWidth, std::uint32_t& _outHeight, std::string& _outError)
{
  if (_atlas.name == "Masks")
  {
    _outWidth = 256;
    _outHeight = 128;
    return true;
  }
  if (_atlas.name.starts_with("Frames"))
  {
    _outWidth = 64;
    _outHeight = 64;
    return true;
  }
  _outError = "no such file";
  return false;
}

constexpr std::string_view ATLASES = "atlas name=Masks file=Masks.dds kind=mask\n"
                                     "atlas name=Frames file=Frames.dds kind=color\n";

[[nodiscard]] std::wstring Wide(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// Parses _text and expects an error that names _line of Test.txt and contains _fragment.
void ExpectError(const std::string& _text, int _line, const std::string& _fragment)
{
  Neuron::SpriteSheet sheet;
  std::string error;
  const bool parsed = Neuron::SpriteSheet::Parse(_text, "Test.txt", FixedSizes, sheet, error);
  const std::wstring message = L"expected line " + std::to_wstring(_line) + L" and '" + Wide(_fragment) + L"', got '" + Wide(error) + L"'";
  Assert::IsFalse(parsed, message.c_str());
  Assert::IsTrue(error.find("Test.txt line " + std::to_string(_line) + ":") != std::string::npos, message.c_str());
  Assert::IsTrue(error.find(_fragment) != std::string::npos, message.c_str());
}

} // namespace

TEST_CLASS(SpriteSheetTests)
{
public:
  TEST_METHOD(AValidManifestNamesItsSpritesAndAtlases)
  {
    const std::string text = "# a comment\n" + std::string(ATLASES) +
                             "sprite name=Icon.Repair atlas=Masks rect=8,8,64,64\n"
                             "sprite name=Panel.Frame atlas=Frames rect=4,8,32,40 slice=8,10,6,12\n";
    Neuron::SpriteSheet sheet;
    std::string error;
    Assert::IsTrue(Neuron::SpriteSheet::Parse(text, "Test.txt", FixedSizes, sheet, error), Wide(error).c_str());
    Assert::AreEqual(std::size_t{2}, sheet.Atlases().size());
    Assert::AreEqual(1u, sheet.Atlases()[0].textureSlot, L"slot 0 is the font atlas");
    Assert::AreEqual(2u, sheet.Atlases()[1].textureSlot);

    const Neuron::Sprite* icon = sheet.Find("Icon.Repair");
    Assert::IsNotNull(icon);
    Assert::IsTrue(icon->kind == Neuron::SpriteKind::Mask);
    Assert::IsFalse(icon->nineSlice);
    Assert::AreEqual(1u, icon->textureSlot);
    Assert::AreEqual(8.0f / 256.0f, icon->u0);
    Assert::AreEqual(8.0f / 128.0f, icon->v0);
    Assert::AreEqual(72.0f / 256.0f, icon->u1);
    Assert::AreEqual(72.0f / 128.0f, icon->v1);
    Assert::AreEqual(4, icon->line);

    const Neuron::Sprite* panel = sheet.Find("Panel.Frame");
    Assert::IsNotNull(panel);
    Assert::IsTrue(panel->kind == Neuron::SpriteKind::Color);
    Assert::IsTrue(panel->nineSlice);
    Assert::AreEqual(8, panel->slices.left);
    Assert::AreEqual(10, panel->slices.top);
    Assert::AreEqual(6, panel->slices.right);
    Assert::AreEqual(12, panel->slices.bottom);
    Assert::AreEqual(1u, panel->atlas);
    Assert::IsNull(sheet.Find("Panel.Missing"));
  }

  TEST_METHOD(ASpriteMayComeBeforeItsAtlas)
  {
    const std::string text = "sprite name=A atlas=Frames rect=0,0,64,64\natlas name=Frames file=Frames.dds kind=color\n";
    Neuron::SpriteSheet sheet;
    std::string error;
    Assert::IsTrue(Neuron::SpriteSheet::Parse(text, "Test.txt", FixedSizes, sheet, error), Wide(error).c_str());
    Assert::IsNotNull(sheet.Find("A"), L"a rect touching the atlas's far edges is inside it");
  }

  TEST_METHOD(EveryValidationErrorNamesItsLine)
  {
    const std::string atlases(ATLASES);
    ExpectError(atlases + "sprite name=A atlas=Nowhere rect=0,0,8,8\n", 3, "unknown atlas 'Nowhere'");
    ExpectError(atlases + "sprite name=A atlas=Masks rect=200,0,64,8\n", 3, "lies outside atlas 'Masks'");
    ExpectError(atlases + "sprite name=A atlas=Frames rect=0,60,8,8\n", 3, "lies outside");
    ExpectError(atlases + "sprite name=A atlas=Frames rect=-4,0,8,8\n", 3, "lies outside");
    ExpectError(atlases + "sprite name=A atlas=Frames rect=0,0,0,8\n", 3, "lies outside");
    ExpectError(atlases + "sprite name=A atlas=Frames rect=0,0,16,16 slice=8,2,9,2\n", 3, "larger than its rect");
    ExpectError(atlases + "sprite name=A atlas=Frames rect=0,0,16,16 slice=2,10,2,7\n", 3, "larger than its rect");
    ExpectError(atlases + "sprite name=A atlas=Frames rect=0,0,8,8\nsprite name=A atlas=Masks rect=0,0,8,8\n", 4,
                "already declared on line 3");
    ExpectError(atlases + "atlas name=Masks file=Other.dds kind=mask\n", 3, "atlas 'Masks' is already declared on line 1");
    ExpectError(atlases + "picture name=A\n", 3, "unknown record 'picture'");
    ExpectError("atlas name=Masks file=Masks.dds kind=shiny\n", 1, "neither mask nor color");
    ExpectError("atlas name=Elsewhere file=Elsewhere.dds kind=mask\n", 1, "no such file");
    ExpectError(atlases + "sprite name=A atlas=Masks\n", 3, "needs rect=");
    ExpectError(atlases + "sprite name=A atlas=Masks rect=0,0,8\n", 3, "four integers");
    ExpectError(atlases + "sprite name=A atlas=Masks rect=0,0,8,8 slice=1,2,3\n", 3, "four integers");
    ExpectError(atlases + "sprite name=A atlas=Masks rect=0,0,8,8 colour=red\n", 3, "has no key 'colour'");
    ExpectError(atlases + "sprite name=A atlas=Masks rect=0,0,8,8 rect=0,0,4,4\n", 3, "given twice");
    ExpectError(atlases + "atlas name=Frames2 file=Frames.dds kind=color\natlas name=Frames3 file=Frames.dds kind=color\n", 4,
                "more than 3 atlases");
  }

  TEST_METHOD(TheShippedManifestLoadsAndEverySpriteLiesInsideItsAtlas)
  {
    const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
    Assert::IsTrue(root.has_value(), L"Assets/Scene.txt is found above the test DLL");
    Neuron::SpriteSheet sheet;
    std::vector<Neuron::DdsImage> images;
    std::string error;
    Assert::IsTrue(Neuron::LoadSpriteSheet(*root / "Ui" / "Sprites.txt", sheet, images, error), Wide(error).c_str());
    Assert::AreEqual(sheet.Atlases().size(), images.size(), L"one image per atlas");

    for (const Neuron::Sprite& sprite : sheet.Sprites())
    {
      const Neuron::DdsImage& image = images[sprite.atlas];
      const Neuron::TexelRect& r = sprite.rect;
      const std::wstring name = Wide(sprite.name);
      Assert::IsTrue(r.x >= 8 && r.y >= 8 && r.x + r.width + 8 <= static_cast<std::int32_t>(image.width) &&
                       r.y + r.height + 8 <= static_cast<std::int32_t>(image.height),
                     (name + L" lies inside its atlas with an 8-texel gutter").c_str());
      Assert::IsTrue(r.x % 4 == 0 && r.y % 4 == 0 && r.width % 4 == 0 && r.height % 4 == 0, (name + L" is on 4-texel boundaries").c_str());
      for (const Neuron::Sprite& other : sheet.Sprites())
      {
        if (&other == &sprite || other.atlas != sprite.atlas)
        {
          continue;
        }
        const Neuron::TexelRect& o = other.rect;
        const bool apart =
          r.x + r.width + 16 <= o.x || o.x + o.width + 16 <= r.x || r.y + r.height + 16 <= o.y || o.y + o.height + 16 <= r.y;
        Assert::IsTrue(apart, (name + L" keeps its gutter from " + Wide(other.name)).c_str());
      }
    }

    // The names runtime code relies on.
    for (const char* name :
         {"Panel.Frame",    "Panel.Fill",      "Panel.Edge",      "Panel.Inset",    "Focus.Ring",       "Icon.Launch",   "Icon.Repair",
          "Icon.Build",     "Icon.Yard",       "Icon.Fleet",      "Icon.Services",  "Status.Warning",   "Button.Idle",   "Button.Hover",
          "Button.Pressed", "Button.Selected", "Button.Disabled", "Button.Pending", "Button.Confirmed", "Button.Refused"})
    {
      Assert::IsNotNull(sheet.Find(name), Wide(name).c_str());
    }
    Assert::IsTrue(sheet.Find("Panel.Frame")->nineSlice && sheet.Find("Button.Idle")->nineSlice);
    Assert::IsTrue(sheet.Find("Icon.Repair")->kind == Neuron::SpriteKind::Mask);
    Assert::IsTrue(sheet.Find("Button.Idle")->kind == Neuron::SpriteKind::Color);

    // Colour art is sRGB with straight alpha, and transparent texels next to an edge carry the
    // edge's colour rather than black, so linear filtering does not darken the rim.
    const Neuron::Sprite& frame = *sheet.Find("Panel.Frame");
    const Neuron::DdsImage& colors = images[frame.atlas];
    Assert::IsTrue(colors.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    Assert::IsTrue(colors.alphaMode == Neuron::DdsAlphaMode::Straight);
    const std::size_t outside = (static_cast<std::size_t>(frame.rect.y + frame.rect.height / 2) * colors.width + frame.rect.x - 1) * 4;
    Assert::AreEqual(std::uint8_t{0}, colors.payload[outside + 3], L"the gutter is transparent");
    Assert::IsTrue(colors.payload[outside] + colors.payload[outside + 1] + colors.payload[outside + 2] > 0, L"and carries the edge colour");
    Assert::IsTrue(images[sheet.Find("Icon.Repair")->atlas].format == DXGI_FORMAT_R8_UNORM);
  }
};

} // namespace NeuronClientTests
