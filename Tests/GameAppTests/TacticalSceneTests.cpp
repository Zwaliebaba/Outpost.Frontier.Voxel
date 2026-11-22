#include "TacticalScene.h"

#include "CppUnitTest.h"

#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace GameAppTests
{

namespace
{

[[nodiscard]] bool Near(const XMFLOAT3& _a, const XMFLOAT3& _b, float _tolerance) noexcept
{
  return std::abs(_a.x - _b.x) <= _tolerance && std::abs(_a.y - _b.y) <= _tolerance && std::abs(_a.z - _b.z) <= _tolerance;
}

[[nodiscard]] Outpost::ViewSetup Camera()
{
  Outpost::ViewSetup view;
  const XMVECTOR eye = XMVectorSet(40.0f, 80.0f, -200.0f, 1.0f);
  const XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
  XMStoreFloat4x4(&view.view, XMMatrixLookAtLH(eye, target, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
  XMStoreFloat4x4(&view.projection, XMMatrixPerspectiveFovLH(XMConvertToRadians(40.0f), 16.0f / 9.0f, 1.0f, 2000.0f));
  XMStoreFloat3(&view.position, eye);
  XMStoreFloat3(&view.forward, XMVector3Normalize(target - eye));
  view.viewportSize = XMFLOAT2{1920.0f, 1080.0f};
  return view;
}

} // namespace

TEST_CLASS(TacticalSceneTests)
{
public:
  TEST_METHOD(DrawConstantsCarryTheCameraIntoVoxelSpace)
  {
    const Outpost::ViewSetup view = Camera();
    const XMMATRIX voxToWorld = XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixRotationY(0.6f) * XMMatrixTranslation(30.0f, -10.0f, 15.0f);
    const Neuron::VoxelDrawConstants constants = Outpost::TacticalScene::DrawConstants(voxToWorld, 2.0f, view);

    XMFLOAT3 cameraBack{};
    XMStoreFloat3(&cameraBack, XMVector3TransformCoord(XMLoadFloat3(&constants.cameraPositionVox), voxToWorld));
    Assert::IsTrue(Near(cameraBack, view.position, 1e-3f), L"the voxel-space camera is the world camera");

    XMFLOAT3 forwardBack{};
    XMStoreFloat3(&forwardBack, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&constants.cameraForwardVox), voxToWorld)));
    Assert::IsTrue(Near(forwardBack, view.forward, 1e-4f), L"and looks the same way");
    Assert::IsTrue(std::abs(XMVectorGetX(XMVector3Length(XMLoadFloat3(&constants.cameraForwardVox))) - 1.0f) < 1e-5f);
    Assert::AreEqual(2.0f, constants.voxelWorldSize);
  }

  TEST_METHOD(TheRenderOriginSnapsToItsGridAroundTheCamera)
  {
    // ADR-010: the scene is drawn relative to a grid point near the camera, so that what is near the
    // camera is near the render origin, wherever in the system the camera is.
    const Outpost::UniversePosition atHome = Outpost::TacticalScene::RenderOriginFor(Outpost::UniversePosition{300.0, -200.0, 500.0});
    Assert::AreEqual(0.0, atHome.x);
    Assert::AreEqual(0.0, atHome.y);
    Assert::AreEqual(0.0, atHome.z, L"500 m is less than half a cell");
    const Outpost::UniversePosition farOut =
      Outpost::TacticalScene::RenderOriginFor(Outpost::UniversePosition{100'000.0, -100'000.0, 600.0});
    Assert::AreEqual(100'352.0, farOut.x, L"98 cells");
    Assert::AreEqual(-100'352.0, farOut.y, L"and the same below");
    Assert::AreEqual(1024.0, farOut.z);
  }

  TEST_METHOD(NothingNearTheCameraIsFarFromTheRenderOrigin)
  {
    // So a float keeps sub-millimetre steps there: at 100 km out, a ship 2 m from the camera moving
    // by a millimetre moves by a millimetre in render space.
    for (const double camera : {0.0, 511.9, 512.1, 99'999.5, 1.0e6, -3.7e5})
    {
      const Outpost::UniversePosition position{camera, camera, camera};
      const Outpost::UniversePosition origin = Outpost::TacticalScene::RenderOriginFor(position);
      Assert::IsTrue(std::abs(camera - origin.x) <= Outpost::TacticalScene::ORIGIN_CELL_METRES / 2.0, std::to_wstring(camera).c_str());
      const float before = static_cast<float>(camera + 2.0 - origin.x);
      const float after = static_cast<float>(camera + 2.001 - origin.x);
      Assert::AreEqual(0.001f, after - before, 1.0e-4f);
    }
  }

  TEST_METHOD(ClipToVoxBringsProjectedPointsBack)
  {
    // The pixel shader unprojects a pixel through clipToVox to build its ray, so a voxel-space
    // point taken to clip space and back must land where it started.
    const Outpost::ViewSetup view = Camera();
    const XMMATRIX voxToWorld = XMMatrixScaling(1.5f, 1.5f, 1.5f) * XMMatrixTranslation(-20.0f, 5.0f, 60.0f);
    const Neuron::VoxelDrawConstants constants = Outpost::TacticalScene::DrawConstants(voxToWorld, 1.5f, view);
    const XMMATRIX voxToClip = XMLoadFloat4x4(&constants.voxToClip);
    const XMMATRIX clipToVox = XMLoadFloat4x4(&constants.clipToVox);
    for (const XMFLOAT3& point : {XMFLOAT3{0, 0, 0}, XMFLOAT3{48, 0, 0}, XMFLOAT3{0, 36, 30}, XMFLOAT3{48, 36, 30}, XMFLOAT3{12, 7, 25}})
    {
      const XMVECTOR clip = XMVector4Transform(XMVectorSet(point.x, point.y, point.z, 1.0f), voxToClip);
      const XMVECTOR back = XMVector4Transform(clip, clipToVox);
      XMFLOAT3 recovered{};
      XMStoreFloat3(&recovered, XMVectorDivide(back, XMVectorSplatW(back)));
      Assert::IsTrue(Near(recovered, point, 1e-2f), L"a hundredth of a voxel at most");
    }
  }
};

} // namespace GameAppTests
