#include "ControlFocus.h"
#include "DockedView.h"
#include "FileSys.h"
#include "FontAtlas.h"
#include "HudBatch.h"
#include "SpriteSheet.h"
#include "TacticalHud.h"

#include "CppUnitTest.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameAppTests
{

namespace
{

constexpr float EDGE_TOLERANCE_PIXELS = 2.0f;

int g_moduleAnchor = 0;

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

[[nodiscard]] bool Overlap(const Neuron::ScreenRect& _a, const Neuron::ScreenRect& _b) noexcept
{
  return _a.left < _b.right && _b.left < _a.right && _a.top < _b.bottom && _b.top < _a.bottom;
}

[[nodiscard]] bool Inside(const Neuron::ScreenRect& _rect, float _width, float _height) noexcept
{
  return _rect.left >= 0.0f && _rect.top >= 0.0f && _rect.right <= _width && _rect.bottom <= _height;
}

[[nodiscard]] const Outpost::DockedControl* Find(const Outpost::DockedViewLayout& _layout, Neuron::ControlId _id)
{
  const auto found = std::ranges::find(_layout.controls, _id, &Outpost::DockedControl::id);
  return found != _layout.controls.end() ? &*found : nullptr;
}

struct Built
{
  Neuron::FontAtlas atlas;
  Neuron::HudBatch batch;
  Outpost::HudLayout hud;
  Outpost::DockedViewLayout docked;
};

/// The tactical HUD and the docked view over it, as the client draws them while docked.
void Build(float _width, float _height, const Outpost::DockedViewInput& _docked, Built& _out, const Neuron::SpriteSheet* _sprites)
{
  const float scale = std::clamp(_height / 1080.0f, 0.6f, 2.0f);
  Assert::IsTrue(_out.atlas.Build(Outpost::TacticalHud::Fonts(scale)), L"the GDI font atlas builds");
  _out.batch.Begin(&_out.atlas, _sprites);
  Outpost::UniverseSnapshot snapshot;
  snapshot.ships.push_back(Outpost::ShipState{"Lancer", "E-114", "Escort", 92, {{"Shields", 88}}});
  snapshot.objectives.push_back(Outpost::ObjectiveState{"Defend the shipyard", true});
  snapshot.resources.push_back(Outpost::ResourceState{"Alloy", 12400, 0});
  Outpost::HudInput hud;
  hud.width = _width;
  hud.height = _height;
  hud.scale = scale;
  hud.snapshot = &snapshot;
  hud.selected = &snapshot.ships[0];
  hud.buildLabels = {"HANGAR", "COMMAND", "TANKS", "TRUSS"};
  _out.hud = Outpost::TacticalHud{}.Build(_out.batch, hud);
  _out.batch.SetLayer(Neuron::HudBatch::FRONT);
  Outpost::DockedViewInput docked = _docked;
  docked.width = _width;
  docked.height = _height;
  docked.scale = scale;
  _out.docked = Outpost::DockedView{}.Build(_out.batch, docked);
}

[[nodiscard]] Outpost::DockedViewInput Services()
{
  Outpost::DockedViewInput input;
  input.section = Outpost::DockedSection::Services;
  input.stationLabel = "Kessler Reach Shipyard";
  input.shipLabel = "Lancer E-114";
  return input;
}

} // namespace

TEST_CLASS(DockedViewTests)
{
public:
  TEST_METHOD(TheRailAndInspectorStayOnScreenAndClearOfTheHudAtThreeResolutions)
  {
    const Neuron::SpriteSheet sprites = ShippedSprites();
    for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, std::pair{1920.0f, 1080.0f}, std::pair{3840.0f, 2160.0f}})
    {
      Built built;
      Build(width, height, Services(), built, &sprites);
      const Outpost::DockedViewLayout& layout = built.docked;
      Assert::IsTrue(Inside(layout.rail, width, height), L"the rail is on screen");
      Assert::IsTrue(Inside(layout.inspector, width, height), L"the inspector is on screen");
      Assert::IsTrue(layout.rail.right < width * 0.1f, L"the rail is narrow and at the left");
      Assert::IsTrue(layout.inspector.left > width * 0.75f, L"the inspector is at the right, the station between them");
      for (const Neuron::ScreenRect& slot : built.hud.buildSlots)
      {
        Assert::IsFalse(Overlap(slot, layout.inspector), L"the inspector clears the build panel");
        Assert::IsFalse(Overlap(slot, layout.rail));
      }
      Assert::IsFalse(Overlap(built.hud.silhouette, layout.inspector), L"and the ship panel");
      Assert::IsFalse(Overlap(built.hud.silhouette, layout.rail));
      for (const Outpost::DockedControl& control : layout.controls)
      {
        const bool inRail = control.rect.left >= layout.rail.left && control.rect.right <= layout.rail.right;
        const bool inInspector = control.rect.left >= layout.inspector.left && control.rect.right <= layout.inspector.right &&
                                 control.rect.bottom <= layout.inspector.bottom;
        Assert::IsTrue(inRail || inInspector, L"every control lies in the rail or the inspector");
      }
      for (const Neuron::GpuHudVertex& vertex : built.batch.Vertices(Neuron::HudBatch::FRONT))
      {
        Assert::IsTrue(vertex.position.x >= -EDGE_TOLERANCE_PIXELS && vertex.position.x <= width + EDGE_TOLERANCE_PIXELS &&
                         vertex.position.y >= -EDGE_TOLERANCE_PIXELS && vertex.position.y <= height + EDGE_TOLERANCE_PIXELS,
                       L"every vertex stays on screen");
      }
    }
  }

  TEST_METHOD(ServicesOfferLaunchAndShowRepairUnavailableWithItsReason)
  {
    Built built;
    Build(1920.0f, 1080.0f, Services(), built, nullptr);
    Assert::AreEqual(std::size_t{6}, built.docked.controls.size(), L"four sections, launch and repair");
    const Outpost::DockedControl* launch = Find(built.docked, Outpost::DockedView::LAUNCH);
    const Outpost::DockedControl* repair = Find(built.docked, Outpost::DockedView::REPAIR);
    Assert::IsNotNull(launch);
    Assert::IsNotNull(repair);
    Assert::IsTrue(launch->enabled);
    Assert::IsFalse(repair->enabled);
    Assert::IsFalse(repair->reason.empty(), L"a disabled control says why");

    Outpost::DockedViewInput other = Services();
    other.section = Outpost::DockedSection::Build;
    Built build;
    Build(1920.0f, 1080.0f, other, build, nullptr);
    Assert::AreEqual(std::size_t{4}, build.docked.controls.size(), L"the other sections hold no actions in slice 2");
  }

  TEST_METHOD(ALaunchTheShipCannotMakeIsDisabledWithTheReason)
  {
    Outpost::DockedViewInput input = Services();
    input.launchAvailable = false;
    input.launchUnavailable = "the ship has no working engine";
    Built built;
    Build(1920.0f, 1080.0f, input, built, nullptr);
    const Outpost::DockedControl* launch = Find(built.docked, Outpost::DockedView::LAUNCH);
    Assert::IsFalse(launch->enabled);
    Assert::AreEqual(std::string("the ship has no working engine"), launch->reason);

    // Pending: not asked twice. Refused by the server: offered again, the refusal shown.
    input = Services();
    input.launchPending = true;
    Built pending;
    Build(1920.0f, 1080.0f, input, pending, nullptr);
    Assert::IsFalse(Find(pending.docked, Outpost::DockedView::LAUNCH)->enabled);
    input = Services();
    input.launchRefused = "the ship has no power plant";
    Built refused;
    Build(1920.0f, 1080.0f, input, refused, nullptr);
    Assert::IsTrue(Find(refused.docked, Outpost::DockedView::LAUNCH)->enabled);
  }

  TEST_METHOD(AClickOnLaunchIsAClickAndOneOnRepairARefusal)
  {
    Built built;
    Build(1920.0f, 1080.0f, Services(), built, nullptr);
    Neuron::ControlFocus focus;
    focus.BeginLayout();
    for (const Outpost::DockedControl& control : built.docked.controls)
    {
      focus.Add(control.id, control.rect, control.enabled, control.reason);
    }
    const auto click = [&](const Neuron::ScreenRect& _rect)
    {
      focus.PointerMove((_rect.left + _rect.right) * 0.5f, (_rect.top + _rect.bottom) * 0.5f);
      const bool taken = focus.PointerDown();
      focus.PointerUp();
      return taken;
    };
    Assert::IsTrue(click(Find(built.docked, Outpost::DockedView::LAUNCH)->rect), L"the press is the UI's, not the view's");
    Assert::AreEqual(Outpost::DockedView::LAUNCH, focus.TakeClick());
    Assert::IsTrue(click(Find(built.docked, Outpost::DockedView::REPAIR)->rect));
    Assert::AreEqual(Neuron::NO_CONTROL, focus.TakeClick());
    Assert::AreEqual(Outpost::DockedView::REPAIR, focus.TakeRefusal());
    focus.PointerMove(960.0f, 540.0f);
    Assert::IsFalse(focus.PointerDown(), L"a press on the station is left to the view");
  }

  TEST_METHOD(EachRailControlSelectsItsSection)
  {
    for (const Outpost::DockedSection section :
         {Outpost::DockedSection::Build, Outpost::DockedSection::Yard, Outpost::DockedSection::Fleet, Outpost::DockedSection::Services})
    {
      Outpost::DockedSection selected = Outpost::DockedSection::Build;
      Assert::IsTrue(Outpost::DockedView::SectionOf(Outpost::DockedView::RailControl(section), selected));
      Assert::IsTrue(selected == section);
    }
    Outpost::DockedSection unchanged = Outpost::DockedSection::Yard;
    Assert::IsFalse(Outpost::DockedView::SectionOf(Outpost::DockedView::LAUNCH, unchanged));
    Assert::IsTrue(unchanged == Outpost::DockedSection::Yard);
  }
};

} // namespace GameAppTests
