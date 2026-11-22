#include "Zone.h"

#include "TestSupport.h"

#include "CppUnitTest.h"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

[[nodiscard]] Outpost::Zone Sphere(std::string _name, double _x, float _radius, float _edge)
{
  Outpost::Zone zone;
  zone.name = std::move(_name);
  zone.shape = Outpost::ZoneShape::Sphere;
  zone.position = Outpost::UniversePosition{_x, 0.0, 0.0};
  zone.sizeMetres = DirectX::XMFLOAT3{_radius, 0.0f, 0.0f};
  zone.edgeFraction = _edge;
  return zone;
}

[[nodiscard]] Outpost::UniversePosition At(double _x, double _y = 0.0, double _z = 0.0)
{
  return Outpost::UniversePosition{_x, _y, _z};
}

} // namespace

TEST_CLASS(ZoneTests)
{
public:
  TEST_METHOD(ImmersionFollowsChapterOnesFormula)
  {
    // Radius 100, edge 0.4: the core reaches 60 m, and the ramp runs from there to the surface.
    const Outpost::Zone zone = Sphere("Cloud", 0.0, 100.0f, 0.4f);
    Assert::AreEqual(1.0f, Outpost::Immersion(zone, At(0.0)), L"the centre");
    Assert::AreEqual(1.0f, Outpost::Immersion(zone, At(60.0)), L"the core's edge");
    Assert::IsTrue(Near(0.5f, Outpost::Immersion(zone, At(0.0, 80.0))), L"halfway across the band: (1 - 0.8) / 0.4");
    Assert::IsTrue(Near(0.125f, Outpost::Immersion(zone, At(0.0, 0.0, -95.0))));
    Assert::AreEqual(0.0f, Outpost::Immersion(zone, At(100.0)), L"on the surface");
    Assert::AreEqual(0.0f, Outpost::Immersion(zone, At(150.0)), L"outside");
  }

  TEST_METHOD(AnEdgeOfZeroIsAHardBorder)
  {
    const Outpost::Zone zone = Sphere("Wall", 0.0, 100.0f, 0.0f);
    Assert::AreEqual(1.0f, Outpost::Immersion(zone, At(99.9)));
    Assert::AreEqual(0.0f, Outpost::Immersion(zone, At(100.1)));
  }

  TEST_METHOD(BoxesAndCylindersMeasureTowardsTheirSurfaceInEachDirection)
  {
    Outpost::Zone box;
    box.shape = Outpost::ZoneShape::Box;
    box.sizeMetres = DirectX::XMFLOAT3{200.0f, 20.0f, 100.0f}; // half extents 100, 10, 50
    box.edgeFraction = 0.5f;
    Assert::IsTrue(Near(0.5f, Outpost::SurfaceRatio(box, At(50.0))));
    Assert::IsTrue(Near(0.5f, Outpost::SurfaceRatio(box, At(0.0, 5.0))));
    Assert::IsTrue(Near(0.8f, Outpost::SurfaceRatio(box, At(10.0, 2.0, 40.0))), L"the largest ratio of the three axes");
    Assert::IsTrue(Near(0.4f, Outpost::Immersion(box, At(0.0, 0.0, 40.0))));
    Assert::IsTrue(Near(0.5f * std::sqrt(200.0f * 200.0f + 20.0f * 20.0f + 100.0f * 100.0f), Outpost::BoundingRadius(box)));

    Outpost::Zone cylinder;
    cylinder.shape = Outpost::ZoneShape::Cylinder;
    cylinder.sizeMetres = DirectX::XMFLOAT3{30.0f, 400.0f, 0.0f}; // radius 30, 400 long along its Y
    Assert::IsTrue(Outpost::Contains(cylinder, At(20.0, 190.0)));
    Assert::IsFalse(Outpost::Contains(cylinder, At(20.0, 210.0)), L"past its end");
    Assert::IsFalse(Outpost::Contains(cylinder, At(25.0, 0.0, 25.0)), L"outside its radius");
    Assert::IsTrue(Near(std::hypot(30.0f, 200.0f), Outpost::BoundingRadius(cylinder)));
  }

  TEST_METHOD(ARotatedZoneTurnsWithItsOrientation)
  {
    Outpost::Zone box;
    box.shape = Outpost::ZoneShape::Box;
    box.sizeMetres = DirectX::XMFLOAT3{200.0f, 10.0f, 10.0f};
    DirectX::XMStoreFloat4(&box.orientation, DirectX::XMQuaternionRotationRollPitchYaw(0.0f, DirectX::XM_PIDIV2, 0.0f));
    Assert::IsTrue(Outpost::Contains(box, At(0.0, 0.0, 90.0)), L"its long axis now lies along world Z");
    Assert::IsFalse(Outpost::Contains(box, At(90.0)));
  }

  TEST_METHOD(TheSmallestZoneWinsAndNestingWorks)
  {
    std::vector<Outpost::Zone> zones{Sphere("Region", 0.0, 1000.0f, 0.0f), Sphere("Pocket", 300.0, 50.0f, 0.2f)};
    zones[0].region = "Kessler Reach";
    zones[1].damagePerSecond = 20.0f;
    Assert::AreEqual(std::string("Pocket"), Outpost::SmallestZoneAt(zones, At(310.0))->name, L"the pocket inside the region");
    Assert::AreEqual(std::string("Region"), Outpost::SmallestZoneAt(zones, At(-300.0))->name);
    Assert::IsNull(Outpost::SmallestZoneAt(zones, At(2000.0)));
    Assert::AreEqual(std::string("Region"), Outpost::SmallestZoneAt(zones, At(310.0), Outpost::ZoneProperty::Region)->name,
                     L"only zones with the property asked for");
  }

  TEST_METHOD(OverlapsAreDecidedBySizeAlone)
  {
    // Neither contains the other; where they overlap, the smaller wins whichever is listed first.
    const std::vector<Outpost::Zone> zones{Sphere("Large", 0.0, 120.0f, 0.0f), Sphere("Small", 150.0, 80.0f, 0.0f)};
    Assert::AreEqual(std::string("Small"), Outpost::SmallestZoneAt(zones, At(100.0))->name);
    const std::vector<Outpost::Zone> reversed{zones[1], zones[0]};
    Assert::AreEqual(std::string("Small"), Outpost::SmallestZoneAt(reversed, At(100.0))->name);
  }

  TEST_METHOD(PropertiesAroundMergeEveryZoneTouchingTheSphere)
  {
    std::vector<Outpost::Zone> zones{Sphere("Region", 0.0, 1000.0f, 0.0f), Sphere("Hazard", 300.0, 50.0f, 0.0f)};
    zones[0].region = "Kessler Reach";
    zones[1].interference = 0.6f;
    const Outpost::ZoneProperty near = Outpost::PropertiesAround(zones, At(240.0), 20.0f);
    Assert::IsTrue(Outpost::HasProperty(near, Outpost::ZoneProperty::Region));
    Assert::IsTrue(Outpost::HasProperty(near, Outpost::ZoneProperty::Hazard), L"10 m outside the hazard, inside the 20 m sphere");
    Assert::IsFalse(Outpost::HasProperty(Outpost::PropertiesAround(zones, At(200.0), 20.0f), Outpost::ZoneProperty::Hazard));
  }

  TEST_METHOD(AHazardScalesWithImmersion)
  {
    std::vector<Outpost::Zone> zones{Sphere("Breach", 0.0, 60.0f, 0.5f)};
    zones[0].damagePerSecond = 20.0f;
    zones[0].interference = 0.6f;
    const Outpost::HazardExposure core = Outpost::HazardAt(zones, At(10.0));
    Assert::AreEqual(20.0f, core.damagePerSecond);
    Assert::AreEqual(0.6f, core.interference);
    const Outpost::HazardExposure edge = Outpost::HazardAt(zones, At(45.0)); // c = 0.75, m = 0.5
    Assert::IsTrue(Near(10.0f, edge.damagePerSecond));
    Assert::IsTrue(Near(0.3f, edge.interference));
    Assert::IsNull(Outpost::HazardAt(zones, At(61.0)).zone);
    zones[0].interference = 3.0f;
    Assert::AreEqual(1.0f, Outpost::HazardAt(zones, At(0.0)).interference, L"interference is at most 1");
  }
};

} // namespace GameCoreTests
