#include "Collision.h"
#include "DockingPoint.h"
#include "SceneDefinition.h"
#include "ShipArchetype.h"
#include "SystemContent.h"

#include "TestSupport.h"

#include "CppUnitTest.h"

#include <algorithm>
#include <string>

using namespace DirectX;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

constexpr std::string_view DOCK_SCENE = R"(model name=Box file=Models/TrussBeam.vox
socket model=Box name=Out class=dock position=0,-40,0 normal=0,-1,0 up=0,0,1
socket model=Box name=Near class=dock position=0,-10,0 normal=0,-1,0 up=0,0,1
socket model=Box name=Pad class=dock position=0,0,0 normal=0,-1,0 up=0,0,1
dockpoint model=Box name=Bay kind=berth entry=Out approach=Near pad=Pad radius=3
model name=Plain file=Models/TrussBeam.vox
instance name=Station model=Box position=0,0,0
instance name=Rock model=Plain position=500,0,0
)";

[[nodiscard]] std::wstring Wide(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

[[nodiscard]] Outpost::SceneDefinition Parsed(std::string_view _text)
{
  Outpost::SceneDefinition scene;
  std::string error;
  Assert::IsTrue(Outpost::ParseScene(_text, scene, error), Wide(error).c_str());
  return scene;
}

[[nodiscard]] bool Mentions(const std::vector<std::string>& _problems, std::string_view _part)
{
  return std::ranges::any_of(_problems, [_part](const std::string& _problem) { return _problem.find(_part) != std::string::npos; });
}

[[nodiscard]] std::string SystemError(std::string_view _text)
{
  Outpost::SystemContent content;
  std::string error;
  Assert::IsFalse(Outpost::ParseSystemContent(_text, "System.txt", Parsed(DOCK_SCENE), content, error));
  return error;
}

/// How far a point lies outside an oriented box; 0 inside.
[[nodiscard]] float DistanceOutside(const Outpost::CollisionBox& _box, const Outpost::UniversePosition& _point)
{
  const XMVECTOR offset = XMVectorSet(static_cast<float>(_point.x - _box.center.x), static_cast<float>(_point.y - _box.center.y),
                                      static_cast<float>(_point.z - _box.center.z), 0.0f);
  XMFLOAT3 local{};
  XMStoreFloat3(&local, XMVector3InverseRotate(offset, XMLoadFloat4(&_box.orientation)));
  const float x = std::max(0.0f, std::abs(local.x) - _box.halfExtentsMetres.x);
  const float y = std::max(0.0f, std::abs(local.y) - _box.halfExtentsMetres.y);
  const float z = std::max(0.0f, std::abs(local.z) - _box.halfExtentsMetres.z);
  return std::sqrt(x * x + y * y + z * z);
}

} // namespace

TEST_CLASS(DockingContentTests)
{
public:
  TEST_METHOD(ADockingPointIsReadWithItsSockets)
  {
    const Outpost::SceneDefinition scene = Parsed(DOCK_SCENE);
    const Outpost::DockingPoint* point = scene.FindModel("Box")->FindDockingPoint("Bay");
    Assert::IsNotNull(point);
    Assert::IsTrue(point->kind == Outpost::DockKind::Berth);
    Assert::AreEqual(3.0f, point->radiusMetres);
    Assert::IsTrue(scene.FindModel("Box")->FindSocket("Pad")->socketClass == Outpost::SocketClass::Dock);
    Assert::IsTrue(Outpost::ValidateScene(scene, AssetRoot()).empty());
  }

  TEST_METHOD(ADockingPathLiesWhereItsSocketsAreAndFacesOut)
  {
    Outpost::SceneDefinition scene = Parsed(DOCK_SCENE);
    scene.FindModel("Box")->SetSize(Neuron::Int3{0, 0, 0}); // pivot at the origin
    const Outpost::DockingPath path = Outpost::PathFor(scene, 0, *scene.FindModel("Box")->FindDockingPoint("Bay"));
    Assert::IsTrue(Near(-40.0f, static_cast<float>(path.entry.z)), L"VOX -Y is world -Z");
    Assert::IsTrue(Near(-10.0f, static_cast<float>(path.approach.z)));
    Assert::IsTrue(Near(0.0f, static_cast<float>(path.pad.z)));
    XMFLOAT3 nose{};
    XMStoreFloat3(&nose, XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), XMLoadFloat4(&path.orientation)));
    Assert::IsTrue(Near(XMFLOAT3{0.0f, 0.0f, -1.0f}, nose), L"a docked ship's nose points out along the pad's normal");
  }

  TEST_METHOD(SocketsNeedAKnownClass)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsFalse(
      Outpost::ParseScene("model name=Box file=Models/TrussBeam.vox\nsocket model=Box name=S position=0,0,0\n", scene, error));
    Assert::IsTrue(error.find("class") != std::string::npos, Wide(error).c_str());
    Assert::IsFalse(
      Outpost::ParseScene("model name=Box file=Models/TrussBeam.vox\nsocket model=Box name=S class=hatch position=0,0,0\n", scene, error));
    Assert::IsTrue(error.find("dock, module, equipment or clamp") != std::string::npos, Wide(error).c_str());
  }

  TEST_METHOD(BrokenDockingPointsAreReported)
  {
    std::string text(DOCK_SCENE);
    text.replace(text.find("name=Near class=dock"), std::string("name=Near class=dock").size(), "name=Near class=module");
    text += "socket model=Box name=Spare class=dock position=0,0,9 normal=0,-1,0 up=0,0,1\n";
    text += "dockpoint model=Box name=Ghost kind=medium entry=Out approach=Nowhere pad=Pad\n";
    const std::vector<std::string> problems = Outpost::ValidateScene(Parsed(text), AssetRoot());
    Assert::IsTrue(Mentions(problems, "uses socket 'Near', which is not a dock socket"));
    Assert::IsTrue(Mentions(problems, "names missing socket 'Nowhere'"));
    Assert::IsTrue(Mentions(problems, "uses socket 'Out' a second time"));
    Assert::IsTrue(Mentions(problems, "dock socket 'Spare' on model 'Box' belongs to no docking point"));

    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsFalse(Outpost::ParseScene(std::string(DOCK_SCENE) + "dockpoint model=Box name=B kind=gate entry=Out approach=Near pad=Pad\n",
                                        scene, error));
    Assert::IsTrue(error.find("berth, medium or large") != std::string::npos);
  }

  TEST_METHOD(AStationIsReadWithItsZones)
  {
    Outpost::SystemContent content;
    std::string error;
    const std::string text = "zone name=Space shape=sphere position=0,0,0 radius=900 region=\"Test Reach\"\n"
                             "zone name=Vent shape=cylinder position=10,0,0 radius=5 length=40 damage=7 interference=0.5 edge=0.2\n"
                             "station name=Yard label=\"The Yard\" region=Space instance=Station\n"
                             "start station=Yard\n";
    Assert::IsTrue(Outpost::ParseSystemContent(text, "System.txt", Parsed(DOCK_SCENE), content, error), Wide(error).c_str());
    Assert::AreEqual(std::size_t{2}, content.zones.size());
    Assert::IsTrue(content.zones[1].shape == Outpost::ZoneShape::Cylinder);
    Assert::AreEqual(40.0f, content.zones[1].sizeMetres.y);
    Assert::AreEqual(std::string("Yard"), content.StationOf("Station")->name);
    Assert::IsNull(content.StationOf("Rock"));
    Assert::AreEqual(std::string("Yard"), content.startStation);
  }

  TEST_METHOD(EveryStationHasADockingPoint)
  {
    const std::string error = SystemError("station name=Quarry label=Quarry instance=Rock\nstart station=Quarry\n");
    Assert::IsTrue(error.find("System.txt line 1: station 'Quarry' has no docking point") != std::string::npos, Wide(error).c_str());
  }

  TEST_METHOD(SystemMistakesNameTheFileAndLine)
  {
    const std::string station = "station name=Yard label=Y instance=Station\n";
    Assert::IsTrue(SystemError(station).find("no 'start' record") != std::string::npos);
    Assert::IsTrue(SystemError(station + "start station=Elsewhere\n").find("line 2: unknown station 'Elsewhere'") != std::string::npos);
    Assert::IsTrue(SystemError("zone name=Z shape=torus position=0,0,0\n").find("line 1: 'shape' must be") != std::string::npos);
    Assert::IsTrue(SystemError("zone name=Z shape=sphere position=0,0,0 size=1,2,3 radius=4\n").find("sized by radius") !=
                   std::string::npos);
    Assert::IsTrue(SystemError("zone name=Z shape=sphere position=0,0,0 radius=4 interference=2\n").find("'interference'") !=
                   std::string::npos);
    Assert::IsTrue(SystemError("station name=Yard label=Y instance=Nowhere\n").find("unknown instance 'Nowhere'") != std::string::npos);
    Assert::IsTrue(SystemError("zone name=Z shape=sphere position=0,0,0 radius=4\nstation name=Yard label=Y region=Z instance=Station\n"
                               "start station=Yard\n")
                     .find("line 2: station 'Yard' names 'Z', which is not a named region") != std::string::npos);
    Assert::IsTrue(SystemError("moon name=M\n").find("unknown keyword 'moon'") != std::string::npos);
  }

  TEST_METHOD(TheShippedContentDocksTheEscortsAndReleasesThemClear)
  {
    const Outpost::SceneDefinition scene = LoadRealScene();
    Outpost::SystemContent system;
    std::string error;
    Assert::IsTrue(Outpost::LoadSystemContent(AssetRoot() / "Content" / "System.txt", scene, system, error), Wide(error).c_str());
    Outpost::ShipContent ships;
    Assert::IsTrue(Outpost::LoadShipContent(AssetRoot() / "Content" / "Ships.txt", ships, error), Wide(error).c_str());
    const Outpost::StationDefinition* start = system.FindStation(system.startStation);
    Assert::IsNotNull(start);

    // Every hull's docking class is offered by a point of the station a new game starts at.
    std::vector<Outpost::UniverseObject> objects;
    for (std::size_t i = 0; i < scene.instances.size(); ++i)
    {
      objects.push_back(Outpost::ObjectFromInstance(scene, i, static_cast<Outpost::ObjectId>(i + 1)));
    }
    const std::vector<Outpost::CollisionBox> bodies = Outpost::StaticBodies(scene, ships, objects);
    for (const Outpost::ShipArchetype& ship : ships.ships)
    {
      bool offered = false;
      for (const std::string& instanceName : start->instances)
      {
        const int index = scene.InstanceIndex(instanceName);
        for (const Outpost::DockingPoint& point : scene.FindModel(scene.instances[static_cast<std::size_t>(index)].model)->dockingPoints)
        {
          if (point.kind != ship.dockingClass)
          {
            continue;
          }
          offered = true;
          // A ship released at the entry meets nothing: its sphere is clear of every static body.
          const Outpost::DockingPath path = Outpost::PathFor(scene, static_cast<std::size_t>(index), point);
          const Outpost::ModelAsset* model = scene.FindModel(ship.model);
          const float halfDiagonal =
            0.5f * model->voxelSize *
            std::sqrt(static_cast<float>(model->size.x * model->size.x + model->size.y * model->size.y + model->size.z * model->size.z));
          for (const Outpost::CollisionBox& body : bodies)
          {
            Assert::IsTrue(
              DistanceOutside(body, path.entry) > halfDiagonal,
              Wide(ship.name + " at " + instanceName + "." + point.name + " entry touches body " + std::to_string(body.object)).c_str());
          }
        }
      }
      Assert::IsTrue(offered, Wide(ship.name + " has no docking point at the start station").c_str());
    }
  }
};

} // namespace GameCoreTests
