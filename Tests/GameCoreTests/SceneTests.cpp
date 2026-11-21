#include "TestSupport.h"

#include "SceneDefinition.h"

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

constexpr std::string_view MINIMAL_SCENE = R"(# a comment
camera target=1,2,3 yaw=10 pitch=20 distance=300 fov=45
model name=Box file=Models/TrussBeam.vox voxelsize=2
socket model=Box name=End position=96,4,4 normal=1,0,0 up=0,0,1
instance name=A model=Box position=0,0,0
instance name=B model=Box parent=A parentsocket=End socket=End
ship instance=A callsign=X-1 class=Frigate hull=50 module="Hull Section (B):34" module=Engines:90
objective text="Hold the line" done=true
resource name=Ore amount=100 perminute=120
select instance=A
)";

} // namespace

TEST_CLASS(SceneTests)
{
public:
  TEST_METHOD(ParsesEveryRecordKind)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsTrue(Outpost::ParseScene(MINIMAL_SCENE, scene, error), std::wstring(error.begin(), error.end()).c_str());
    Assert::AreEqual(45.0f, scene.camera.fovYDegrees);
    Assert::AreEqual(std::size_t{1}, scene.models.size());
    Assert::AreEqual(2.0f, scene.models[0].voxelSize);
    Assert::AreEqual(std::size_t{1}, scene.models[0].sockets.size());
    Assert::AreEqual(std::size_t{2}, scene.instances.size());
    Assert::AreEqual(std::string("End"), scene.instances[1].parentSocket);
    const Outpost::ShipDefinition* ship = scene.FindShip("A");
    Assert::IsNotNull(ship);
    Assert::AreEqual(std::size_t{2}, ship->modules.size());
    Assert::AreEqual(std::string("Hull Section (B)"), ship->modules[0].name);
    Assert::AreEqual(34, ship->modules[0].healthPercent);
    Assert::AreEqual(std::size_t{1}, scene.objectives.size());
    Assert::IsTrue(scene.objectives[0].complete);
    Assert::AreEqual(std::int64_t{120}, scene.resources[0].perMinute);
    Assert::AreEqual(std::string("A"), scene.selectedShip);
  }

  TEST_METHOD(ReportsTheLineOfAnUnknownKeyword)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsFalse(Outpost::ParseScene("camera yaw=1\nspaceship name=Q\n", scene, error));
    Assert::IsTrue(error.find("line 2") != std::string::npos, L"the error names the line");
  }

  TEST_METHOD(RejectsMalformedModules)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsFalse(Outpost::ParseScene("ship instance=A callsign=X class=Y module=NoPercent\n", scene, error));
  }

  TEST_METHOD(ValidationFindsBrokenReferences)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsTrue(Outpost::ParseScene("model name=Box file=Models/TrussBeam.vox\n"
                                       "model name=Ghost file=Models/DoesNotExist.vox\n"
                                       "instance name=A model=Box\n"
                                       "instance name=B model=Nothing\n"
                                       "instance name=C model=Box parent=A parentsocket=Nope socket=Nope\n"
                                       "ship instance=Z callsign=Q class=Escort\n",
                                       scene, error));
    const std::vector<std::string> problems = Outpost::ValidateScene(scene, AssetRoot());
    auto mentions = [&](std::string_view _text)
    { return std::ranges::any_of(problems, [&](const std::string& _problem) { return _problem.find(_text) != std::string::npos; }); };
    Assert::IsTrue(mentions("DoesNotExist.vox"), L"missing model file");
    Assert::IsTrue(mentions("unknown model 'Nothing'"), L"unknown model");
    Assert::IsTrue(mentions("socket 'Nope'"), L"missing socket");
    Assert::IsTrue(mentions("unknown instance 'Z'"), L"ship without an instance");
  }

  TEST_METHOD(TheShippedSceneIsConsistent)
  {
    const Outpost::SceneDefinition scene = LoadRealScene();
    const std::vector<std::string> problems = Outpost::ValidateScene(scene, AssetRoot());
    std::wstring message;
    for (const std::string& problem : problems)
    {
      message += std::wstring(problem.begin(), problem.end()) + L"\n";
    }
    Assert::IsTrue(problems.empty(), message.c_str());
    Assert::IsNotNull(scene.FindShip(scene.selectedShip), L"the default selection is a ship");
    for (const Outpost::BuildItemDefinition& item : scene.buildItems)
    {
      Assert::IsNotNull(scene.FindModel(item.model), L"every build item names a model");
    }
  }

  TEST_METHOD(ChildInstancesFollowTheirParent)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsTrue(Outpost::ParseScene(MINIMAL_SCENE, scene, error));
    scene.models[0].SetSize(Neuron::Int3{96, 8, 8});
    const DirectX::XMMATRIX before = Outpost::ModelToWorld(scene, 1);
    scene.instances[0].position = DirectX::XMFLOAT3{100.0f, 0.0f, 0.0f};
    const DirectX::XMMATRIX after = Outpost::ModelToWorld(scene, 1);
    const DirectX::XMFLOAT3 moved{Row(after, 3).x - Row(before, 3).x, Row(after, 3).y - Row(before, 3).y,
                                  Row(after, 3).z - Row(before, 3).z};
    Assert::IsTrue(Near(moved, DirectX::XMFLOAT3{100.0f, 0.0f, 0.0f}, 1e-3f));
  }

  TEST_METHOD(ShippedBlueprintExtensionSitsOnTheHangarDock)
  {
    const Outpost::SceneDefinition scene = LoadRealScene();
    const int hangar = scene.InstanceIndex("Hangar");
    const int extension = scene.InstanceIndex("Extension");
    Assert::IsTrue(hangar >= 0 && extension >= 0);
    const Outpost::SceneInstance& instance = scene.instances[static_cast<std::size_t>(extension)];
    Assert::IsTrue(instance.style == Outpost::InstanceStyle::Blueprint);
    const Outpost::ModelAsset* hangarModel = scene.FindModel("HangarBay");
    const Outpost::ModelAsset* sectionModel = scene.FindModel(instance.model);
    const Neuron::Socket* dock = hangarModel->FindSocket(instance.parentSocket);
    const Neuron::Socket* mount = sectionModel->FindSocket(instance.socket);
    Assert::IsTrue(dock != nullptr && mount != nullptr);
    const DirectX::XMMATRIX dockWorld = Outpost::SocketWorld(scene, static_cast<std::size_t>(hangar), *dock);
    const DirectX::XMMATRIX mountWorld = Outpost::SocketWorld(scene, static_cast<std::size_t>(extension), *mount);
    Assert::IsTrue(Near(Row(dockWorld, 3), Row(mountWorld, 3), 1e-3f), L"the extension's mount is at the hangar's dock");
    const DirectX::XMFLOAT3 dockNormal = Row(dockWorld, 2);
    Assert::IsTrue(Near(Row(mountWorld, 2), DirectX::XMFLOAT3{-dockNormal.x, -dockNormal.y, -dockNormal.z}, 1e-4f));
  }
};

} // namespace GameCoreTests
