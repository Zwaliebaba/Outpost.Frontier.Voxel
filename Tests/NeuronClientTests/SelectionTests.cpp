#include "OrbitCamera.h"
#include "PointerGesture.h"
#include "RayBox.h"
#include "ScreenProjection.h"

#include "CppUnitTest.h"

#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace NeuronClientTests
{

namespace
{

[[nodiscard]] bool Near(float _a, float _b, float _tolerance = 1e-4f) noexcept
{
  return std::abs(_a - _b) <= _tolerance;
}

[[nodiscard]] bool Near(const XMFLOAT3& _a, const XMFLOAT3& _b, float _tolerance = 1e-4f) noexcept
{
  return Near(_a.x, _b.x, _tolerance) && Near(_a.y, _b.y, _tolerance) && Near(_a.z, _b.z, _tolerance);
}

constexpr float WIDTH = 1920.0f;
constexpr float HEIGHT = 1080.0f;

// A camera at the origin looking down +Z.
[[nodiscard]] XMMATRIX ViewProjection() noexcept
{
  const XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(0, 0, 0, 1), XMVectorSet(0, 0, 1, 1), XMVectorSet(0, 1, 0, 0));
  return view * XMMatrixPerspectiveFovLH(XMConvertToRadians(40.0f), WIDTH / HEIGHT, 1.0f, 1000.0f);
}

[[nodiscard]] Neuron::OrientedBox Box(XMFLOAT3 _center, float _radius) noexcept
{
  Neuron::OrientedBox box{};
  box.center = _center;
  box.radius = XMFLOAT3{_radius, _radius, _radius};
  XMStoreFloat3x3(&box.axes, XMMatrixIdentity());
  return box;
}

} // namespace

TEST_CLASS(SelectionTests)
{
public:
  TEST_METHOD(BoxAheadProjectsAroundTheScreenCentre)
  {
    const Neuron::ProjectedBounds bounds =
      Neuron::ProjectBox(Neuron::BoxCorners(Box(XMFLOAT3{0, 0, 100}, 5)), ViewProjection(), WIDTH, HEIGHT);
    Assert::IsTrue(bounds.visibility == Neuron::ScreenVisibility::OnScreen);
    Assert::IsFalse(bounds.clippedByNearPlane);
    Assert::IsTrue(bounds.rect.left < WIDTH * 0.5f && bounds.rect.right > WIDTH * 0.5f);
    Assert::IsTrue(bounds.rect.top < HEIGHT * 0.5f && bounds.rect.bottom > HEIGHT * 0.5f);
    Assert::IsTrue(Near(bounds.rect.left + bounds.rect.right, WIDTH, 0.5f), L"symmetric about the centre");
  }

  TEST_METHOD(BoxBehindTheCameraIsReportedNotMirrored)
  {
    const Neuron::ProjectedBounds bounds =
      Neuron::ProjectBox(Neuron::BoxCorners(Box(XMFLOAT3{30, 0, -100}, 5)), ViewProjection(), WIDTH, HEIGHT);
    Assert::IsTrue(bounds.visibility == Neuron::ScreenVisibility::BehindCamera);
    Assert::IsTrue(bounds.edgeDirection.x > 0.9f, L"the edge indicator points right, toward the ship's side");
  }

  TEST_METHOD(BoxBesideTheViewIsOffScreenWithADirection)
  {
    const Neuron::ProjectedBounds bounds =
      Neuron::ProjectBox(Neuron::BoxCorners(Box(XMFLOAT3{-500, 0, 100}, 5)), ViewProjection(), WIDTH, HEIGHT);
    Assert::IsTrue(bounds.visibility == Neuron::ScreenVisibility::OffScreen);
    Assert::IsTrue(bounds.edgeDirection.x < -0.9f);
    const Neuron::ProjectedBounds above =
      Neuron::ProjectBox(Neuron::BoxCorners(Box(XMFLOAT3{0, 400, 100}, 5)), ViewProjection(), WIDTH, HEIGHT);
    Assert::IsTrue(above.edgeDirection.y < -0.9f, L"screen y grows downward, so up is negative");
  }

  TEST_METHOD(BoxStraddlingTheNearPlaneIsClippedNotLost)
  {
    const Neuron::ProjectedBounds bounds =
      Neuron::ProjectBox(Neuron::BoxCorners(Box(XMFLOAT3{0, -3, 0}, 4)), ViewProjection(), WIDTH, HEIGHT);
    Assert::IsTrue(bounds.clippedByNearPlane);
    Assert::IsTrue(bounds.visibility == Neuron::ScreenVisibility::OnScreen);
    Assert::IsTrue(bounds.rect.bottom > HEIGHT, L"the part below the eye extends past the bottom edge");
  }

  TEST_METHOD(PickRayThroughTheCentreHitsTheBoxAhead)
  {
    const XMMATRIX inverse = XMMatrixInverse(nullptr, ViewProjection());
    const Neuron::Ray ray = Neuron::PickRay(WIDTH * 0.5f - 0.5f, HEIGHT * 0.5f - 0.5f, inverse, WIDTH, HEIGHT);
    Assert::IsTrue(Near(ray.direction, XMFLOAT3{0, 0, 1}, 1e-3f));
    float distance = 0.0f;
    XMFLOAT3 normal{};
    Assert::IsTrue(Neuron::IntersectRayBox(Box(XMFLOAT3{0, 0, 100}, 5), ray, true, distance, normal));
    Assert::IsTrue(Near(distance + ray.origin.z, 95.0f, 1e-2f), L"enters at the near face");
    Assert::IsTrue(Near(normal, XMFLOAT3{0, 0, -1}));
    Assert::IsFalse(Neuron::IntersectRayBox(Box(XMFLOAT3{50, 0, 100}, 5), ray, true, distance, normal), L"misses a box off to the side");
    Assert::IsFalse(Neuron::IntersectRayBox(Box(XMFLOAT3{0, 0, -100}, 5), ray, true, distance, normal), L"never picks behind the eye");
  }

  TEST_METHOD(PickRayAtACornerLeansOutward)
  {
    const XMMATRIX inverse = XMMatrixInverse(nullptr, ViewProjection());
    const Neuron::Ray ray = Neuron::PickRay(0.0f, 0.0f, inverse, WIDTH, HEIGHT);
    Assert::IsTrue(ray.direction.x < 0.0f && ray.direction.y > 0.0f && ray.direction.z > 0.0f, L"top-left pixel looks left and up");
  }

  TEST_METHOD(ClickAndDragAreDistinguished)
  {
    Neuron::PointerGesture gesture;
    float dx = 0.0f;
    float dy = 0.0f;
    gesture.Press(100, 100, 1000);
    Assert::IsFalse(gesture.Move(102, 101, dx, dy), L"a small wobble is not a drag");
    Assert::IsTrue(gesture.Release(102, 101, 1100), L"press and release in place is a click");

    gesture.Press(100, 100, 2000);
    Assert::IsTrue(gesture.Move(130, 100, dx, dy), L"past the threshold it is a drag");
    Assert::IsFalse(gesture.Release(130, 100, 2100), L"a drag never selects");

    gesture.Press(100, 100, 3000);
    Assert::IsFalse(gesture.Release(100, 100, 3000 + Neuron::PointerGesture::CLICK_MAX_MILLISECONDS + 1), L"a long hold is not a click");
  }

  TEST_METHOD(OrbitCameraStaysWithinLimits)
  {
    Neuron::OrbitCamera camera;
    camera.SetPose(Neuron::CameraPose{XMFLOAT3{0, 0, 0}, 0.0f, 30.0f, 300.0f, 40.0f});
    camera.Orbit(0.0f, 100000.0f);
    Assert::IsTrue(camera.Pose().pitchDegrees <= Neuron::OrbitCamera::MAX_PITCH_DEGREES);
    camera.Zoom(1000.0f);
    Assert::IsTrue(camera.Pose().distance >= Neuron::OrbitCamera::MIN_DISTANCE);
    camera.Zoom(-1000.0f);
    Assert::IsTrue(camera.Pose().distance <= Neuron::OrbitCamera::MAX_DISTANCE);
    const XMFLOAT3 position = camera.Position();
    const XMFLOAT3 forward = camera.Forward();
    const float distance = camera.Pose().distance;
    Assert::IsTrue(Near(XMFLOAT3{position.x + forward.x * distance, position.y + forward.y * distance, position.z + forward.z * distance},
                        camera.Pose().target, 1e-2f),
                   L"the camera looks at its target");
  }
};

} // namespace NeuronClientTests
