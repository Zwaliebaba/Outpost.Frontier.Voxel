#include "TestSupport.h"

#include "SceneDefinition.h"
#include "UniverseObject.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

/// How far a rebuilt matrix may sit from the original: the decomposition rounds in float, so the
/// tolerance grows with the size of what it rounds.
[[nodiscard]] bool NearMatrix(DirectX::FXMMATRIX _a, DirectX::CXMMATRIX _b) noexcept
{
  for (int row = 0; row < 4; ++row)
  {
    const DirectX::XMFLOAT3 a = Row(_a, row);
    const DirectX::XMFLOAT3 b = Row(_b, row);
    const float magnitude = std::max({1.0f, std::abs(b.x), std::abs(b.y), std::abs(b.z)});
    if (!Near(a, b, 1e-4f * magnitude))
    {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool Parses(std::string_view _instanceLine, Outpost::SceneDefinition& _outScene, std::string& _outError)
{
  const std::string text = "camera target=0,0,0 yaw=0 pitch=20 distance=300 fov=45\nmodel name=Box file=Models/TrussBeam.vox\n" +
                           std::string(_instanceLine) + "\n";
  _outScene = Outpost::SceneDefinition{};
  return Outpost::ParseScene(text, _outScene, _outError);
}

} // namespace

TEST_CLASS(UniverseObjectTests)
{
public:
  TEST_METHOD(EveryInstancesPoseReproducesItsPlacement)
  {
    // Attached instances included: their poses follow the socket chain through the pivots.
    const Outpost::SceneDefinition scene = LoadRealScene();
    for (std::size_t i = 0; i < scene.instances.size(); ++i)
    {
      const Outpost::UniverseObject object = Outpost::ObjectFromInstance(scene, i, static_cast<Outpost::ObjectId>(i + 1));
      const std::wstring name(scene.instances[i].name.begin(), scene.instances[i].name.end());
      Assert::IsTrue(NearMatrix(Outpost::ObjectToWorld(object), Outpost::ModelToWorld(scene, i)), name.c_str());
    }
  }

  TEST_METHOD(AnObjectCarriesItsInstancesNamesAndOwner)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsTrue(Parses("instance name=Mine model=Box position=10,20,30 owner=player", scene, error));
    const Outpost::UniverseObject object = Outpost::ObjectFromInstance(scene, 0, 7);
    Assert::AreEqual(Outpost::ObjectId{7}, object.id);
    Assert::AreEqual(std::string("Mine"), object.name);
    Assert::AreEqual(std::string("Box"), object.model);
    Assert::IsTrue(object.owner == Outpost::Owner::Player);
    Assert::IsTrue(object.pose.position == Outpost::UniversePosition{10.0, 20.0, 30.0});
  }

  TEST_METHOD(TheOwnerTagIsParsed)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsTrue(Parses("instance name=A model=Box position=0,0,0", scene, error));
    Assert::IsTrue(scene.instances[0].owner == Outpost::Owner::Neutral, L"neutral by default");
    Assert::IsTrue(Parses("instance name=A model=Box position=0,0,0 owner=hostile", scene, error));
    Assert::IsTrue(scene.instances[0].owner == Outpost::Owner::Hostile);
    Assert::IsFalse(Parses("instance name=A model=Box position=0,0,0 owner=ally", scene, error));
    Assert::IsTrue(error.find('3') != std::string::npos, L"the error names the line");
  }

  TEST_METHOD(SceneContentCarriesEveryModelsSize)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsTrue(Outpost::LoadSceneContent(AssetRoot(), scene, error), std::wstring(error.begin(), error.end()).c_str());
    for (const Outpost::ModelAsset& model : scene.models)
    {
      Assert::IsTrue(model.size.x > 0 && model.size.y > 0 && model.size.z > 0);
    }
  }
};

} // namespace GameCoreTests
