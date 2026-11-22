#include "FileSys.h"
#include "HazardWarning.h"
#include "SceneDefinition.h"
#include "SystemContent.h"

#include "CppUnitTest.h"

#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameAppTests
{

namespace
{

int g_moduleAnchor = 0;

/// A spherical zone on the X axis, 100 m in radius, with the outer half its edge.
[[nodiscard]] Outpost::Zone Cloud(std::string _name, double _x, float _damagePerSecond, float _interference)
{
  Outpost::Zone zone;
  zone.name = std::move(_name);
  zone.shape = Outpost::ZoneShape::Sphere;
  zone.position = Outpost::UniversePosition{_x, 0.0, 0.0};
  zone.sizeMetres = DirectX::XMFLOAT3{100.0f, 0.0f, 0.0f};
  zone.edgeFraction = 0.5f;
  zone.damagePerSecond = _damagePerSecond;
  zone.interference = _interference;
  return zone;
}

[[nodiscard]] Outpost::UniversePosition At(double _x, double _y = 0.0, double _z = 0.0)
{
  return Outpost::UniversePosition{_x, _y, _z};
}

} // namespace

TEST_CLASS(HazardWarningTests)
{
public:
  TEST_METHOD(TheWarningAppearsInsideTheShippedBreachAndNotOutside)
  {
    const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
    Assert::IsTrue(root.has_value(), L"Assets/Scene.txt is found above the test DLL");
    Outpost::SceneDefinition scene;
    Outpost::SystemContent system;
    std::string error;
    Assert::IsTrue(Outpost::LoadSceneContent(*root, scene, error), std::wstring(error.begin(), error.end()).c_str());
    Assert::IsTrue(Outpost::LoadSystemContent(*root / "Content" / "System.txt", scene, system, error),
                   std::wstring(error.begin(), error.end()).c_str());
    const Outpost::Zone* breach = system.FindZone("FrigateBreach");
    Assert::IsNotNull(breach, L"the system has the frigate's breach");
    const float radius = breach->sizeMetres.x;
    const Outpost::UniversePosition& center = breach->position;

    const Outpost::HazardWarning inside = Outpost::HazardWarningAt(system.zones, center, false);
    Assert::IsTrue(inside.Active(), L"at the heart of the breach");
    Assert::AreEqual(std::string("FrigateBreach"), inside.zone);
    Assert::IsTrue(inside.damaging);
    Assert::AreEqual(60, inside.scannerLossPercent, L"the zone's interference of 0.6, in full");

    const Outpost::UniversePosition within{center.x + static_cast<double>(radius) - 1.0, center.y, center.z};
    Assert::IsTrue(Outpost::HazardWarningAt(system.zones, within, false).Active(), L"a metre inside its surface");

    const Outpost::UniversePosition beyond{center.x + static_cast<double>(radius) + 1.0, center.y, center.z};
    const Outpost::HazardWarning outside = Outpost::HazardWarningAt(system.zones, beyond, false);
    Assert::IsFalse(outside.Active(), L"a metre outside it, in the shipyard's region, which is no hazard");
    Assert::IsTrue(outside.zone.empty());
  }

  TEST_METHOD(TheWarningWeakensAcrossTheZonesEdge)
  {
    // Radius 100 with an edge of 0.5: the core reaches 50 m, and the ramp runs from there to the surface.
    const std::vector<Outpost::Zone> zones{Cloud("Cloud", 0.0, 20.0f, 0.6f)};
    Assert::AreEqual(60, Outpost::HazardWarningAt(zones, At(40.0), false).scannerLossPercent, L"in the core");
    const Outpost::HazardWarning halfway = Outpost::HazardWarningAt(zones, At(0.0, 75.0), false);
    Assert::AreEqual(0.5f, halfway.immersion, 1e-4f);
    Assert::AreEqual(30, halfway.scannerLossPercent, L"half the immersion, half the interference");
    Assert::IsTrue(halfway.damaging, L"a weaker dose is still a dose");

    const Outpost::HazardWarning rim = Outpost::HazardWarningAt(zones, At(0.0, 0.0, 99.9), false);
    Assert::IsTrue(rim.Active(), L"just inside the surface");
    Assert::AreEqual(1, rim.scannerLossPercent, L"the least interference still shows");
    Assert::IsFalse(Outpost::HazardWarningAt(zones, At(100.0), false).Active(), L"on the surface the immersion is nothing");
    Assert::IsFalse(Outpost::HazardWarningAt(zones, At(250.0), false).Active(), L"outside");
  }

  TEST_METHOD(AProtectedShipIsWarnedOfInterferenceButNotOfDamage)
  {
    const std::vector<Outpost::Zone> zones{Cloud("Cloud", 0.0, 20.0f, 0.6f), Cloud("Vent", 1000.0, 20.0f, 0.0f)};
    const Outpost::HazardWarning blinded = Outpost::HazardWarningAt(zones, At(0.0), true);
    Assert::IsFalse(blinded.damaging, L"a ship the station carries takes no damage (D16)");
    Assert::AreEqual(60, blinded.scannerLossPercent);
    Assert::IsTrue(blinded.Active());
    Assert::IsFalse(Outpost::HazardWarningAt(zones, At(1000.0), true).Active(), L"damage alone is nothing to a protected ship");
  }

  TEST_METHOD(AZoneWarnsOnlyOfWhatItDoes)
  {
    const std::vector<Outpost::Zone> zones{Cloud("Vent", 0.0, 20.0f, 0.0f), Cloud("Fog", 1000.0, 0.0f, 0.4f)};
    const Outpost::HazardWarning vent = Outpost::HazardWarningAt(zones, At(0.0), false);
    Assert::IsTrue(vent.damaging);
    Assert::AreEqual(0, vent.scannerLossPercent);
    const Outpost::HazardWarning fog = Outpost::HazardWarningAt(zones, At(1000.0), false);
    Assert::IsFalse(fog.damaging);
    Assert::AreEqual(40, fog.scannerLossPercent);
  }

  TEST_METHOD(TheSmallestPhysicalZoneDecidesAndARegionIsNoHazard)
  {
    // A damaging cloud with a smaller pocket of fog inside it, both inside a named region.
    std::vector<Outpost::Zone> zones{Cloud("Region", 0.0, 0.0f, 0.0f), Cloud("Cloud", 0.0, 20.0f, 0.0f), Cloud("Pocket", 0.0, 0.0f, 0.8f)};
    zones[0].sizeMetres.x = 5000.0f;
    zones[0].region = "Test Reach";
    zones[2].sizeMetres.x = 30.0f;
    const Outpost::HazardWarning pocket = Outpost::HazardWarningAt(zones, At(5.0), false);
    Assert::AreEqual(std::string("Pocket"), pocket.zone, L"only the physical zone's effects apply");
    Assert::IsFalse(pocket.damaging);
    Assert::AreEqual(80, pocket.scannerLossPercent);
    Assert::AreEqual(std::string("Cloud"), Outpost::HazardWarningAt(zones, At(40.0), false).zone);
    Assert::IsFalse(Outpost::HazardWarningAt(zones, At(2000.0), false).Active(), L"in the region alone");
  }
};

} // namespace GameAppTests
