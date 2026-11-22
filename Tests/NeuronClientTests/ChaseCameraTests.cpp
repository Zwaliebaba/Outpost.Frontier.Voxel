#include "ChaseCamera.h"

#include "CppUnitTest.h"

#include <DirectXMath.h>

#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

constexpr float FRAME_SECONDS = 1.0f / 60.0f;

[[nodiscard]] Neuron::ChaseCameraDesc Desc()
{
  Neuron::ChaseCameraDesc desc;
  desc.upMetres = 6.0f;
  desc.backMetres = 25.0f;
  desc.lookSideDegrees = 20.0f;
  desc.lookUpDegrees = 7.0f;
  desc.lookDownDegrees = 25.0f;
  return desc;
}

void Run(Neuron::ChaseCamera& _camera, const Neuron::ChaseInput& _input, float _seconds)
{
  for (int i = 0; i < static_cast<int>(std::lround(_seconds / FRAME_SECONDS)); ++i)
  {
    Neuron::ChaseInput input = _input;
    input.frameSeconds = FRAME_SECONDS;
    _camera.Update(input);
  }
}

[[nodiscard]] float Dot(const DirectX::XMFLOAT3& _a, const DirectX::XMFLOAT3& _b)
{
  return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z;
}

} // namespace

TEST_CLASS(ChaseCameraTests)
{
public:
  TEST_METHOD(AtRestItSitsTheAuthoredOffsetBehindAndAbove)
  {
    Neuron::ChaseCamera camera;
    camera.Reset(Desc(), DirectX::XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f});
    Run(camera, Neuron::ChaseInput{}, 2.0f);
    const DirectX::XMFLOAT3 offset = camera.OffsetFromShip();
    Assert::AreEqual(0.0f, offset.x, 1.0e-4f);
    Assert::AreEqual(6.0f, offset.y, 1.0e-4f, L"6 m up");
    Assert::AreEqual(-25.0f, offset.z, 1.0e-4f, L"25 m back from a ship facing +Z");
    Assert::AreEqual(1.0f, camera.Forward().z, 1.0e-5f, L"looking along the nose");
  }

  TEST_METHOD(ItLagsWhileTheShipAccelerates)
  {
    Neuron::ChaseCamera camera;
    camera.Reset(Desc(), DirectX::XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f});
    Neuron::ChaseInput input;
    float accelerating = 0.0f;
    // From rest to 80 m/s in half a second, as the light fighter does.
    for (int i = 1; i <= 30; ++i)
    {
      input.shipVelocityMetresPerSecond = DirectX::XMFLOAT3{0.0f, 0.0f, 80.0f * static_cast<float>(i) / 30.0f};
      input.frameSeconds = FRAME_SECONDS;
      camera.Update(input);
      accelerating = std::max(accelerating, camera.BackDistanceMetres());
    }
    Run(camera, input, 5.0f);
    const float cruising = camera.BackDistanceMetres();
    Assert::AreEqual(25.0f + 0.05f * 80.0f, cruising, 0.01f, L"steady: 0.05 s of travel behind the offset");
    Assert::IsTrue(accelerating > cruising + 2.0f, L"the ship pulls away while it accelerates");
  }

  TEST_METHOD(ItLooksAheadIntoTheTurn)
  {
    Neuron::ChaseCamera camera;
    camera.Reset(Desc(), DirectX::XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f});
    Neuron::ChaseInput input;
    input.yawThrottle = 1.0f;
    Run(camera, input, 2.0f);
    const float sideways = std::atan2(camera.Forward().x, camera.Forward().z);
    Assert::AreEqual(DirectX::XMConvertToRadians(20.0f), sideways, 0.01f, L"20 degrees right of the nose");

    input.yawThrottle = 0.0f;
    input.pitchThrottle = 1.0f; // nose down
    Run(camera, input, 2.0f);
    Assert::AreEqual(-std::sin(DirectX::XMConvertToRadians(25.0f)), camera.Forward().y, 0.01f, L"25 degrees down");
    input.pitchThrottle = -1.0f;
    Run(camera, input, 2.0f);
    Assert::AreEqual(std::sin(DirectX::XMConvertToRadians(7.0f)), camera.Forward().y, 0.01f, L"7 degrees up");
  }

  TEST_METHOD(ItsTurnRateIsLimited)
  {
    // A ship snapped round to face +X: the camera does not follow in one frame.
    Neuron::ChaseCamera camera;
    camera.Reset(Desc(), DirectX::XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f});
    Neuron::ChaseInput input;
    DirectX::XMStoreFloat4(&input.shipOrientation, DirectX::XMQuaternionRotationRollPitchYaw(0.0f, DirectX::XM_PIDIV2, 0.0f));
    input.frameSeconds = FRAME_SECONDS;
    camera.Update(input);
    Assert::IsTrue(camera.Forward().z > 0.99f, L"one frame turns it by at most the acceleration limit");
    Run(camera, input, 3.0f);
    Assert::AreEqual(1.0f, camera.Forward().x, 1.0e-3f, L"and it gets there");
  }

  TEST_METHOD(ItKeepsItsOwnUpWhileTheShipBanksAndLevelsAfter)
  {
    Neuron::ChaseCamera camera;
    camera.Reset(Desc(), DirectX::XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f});
    Neuron::ChaseInput input;
    input.yawThrottle = 1.0f;
    // The ship banked 30 degrees into a right turn.
    DirectX::XMStoreFloat4(&input.shipOrientation,
                           DirectX::XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, DirectX::XMConvertToRadians(-30.0f)));
    Run(camera, input, 2.0f);
    // The look-ahead target dips with the bank, so the camera pitches a little; what must not happen
    // is the horizon tilting with the ship. Swinging along a tilted arc leaves a residual roll of
    // about a degree (parallel transport), against the ship's 30.
    const DirectX::XMFLOAT3 up = camera.Up();
    const DirectX::XMFLOAT3 forward = camera.Forward();
    DirectX::XMFLOAT3 right{};
    DirectX::XMStoreFloat3(&right, DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&up), DirectX::XMLoadFloat3(&forward)));
    Assert::IsTrue(std::abs(right.y) < std::sin(DirectX::XMConvertToRadians(2.0f)), L"the bank does not roll the view");

    input.yawThrottle = 0.0f;
    Run(camera, input, 8.0f);
    const DirectX::XMFLOAT3 shipUp{std::sin(DirectX::XMConvertToRadians(30.0f)), std::cos(DirectX::XMConvertToRadians(30.0f)), 0.0f};
    Assert::IsTrue(Dot(camera.Up(), shipUp) > 0.999f, L"out of the turn the view settles onto the ship's up");
  }

  TEST_METHOD(AShakeDecaysAtTheSameRateAtAnyFrameRate)
  {
    Neuron::ChaseCamera at30;
    Neuron::ChaseCamera at144;
    at30.Reset(Desc(), DirectX::XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f});
    at144.Reset(Desc(), DirectX::XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f});
    at30.Shake(0.02f);
    at144.Shake(0.02f);
    Neuron::ChaseInput input;
    for (int i = 0; i < 6; ++i)
    {
      input.frameSeconds = 1.0f / 30.0f;
      at30.Update(input);
    }
    for (int i = 0; i < 29; ++i)
    {
      input.frameSeconds = 1.0f / 144.0f;
      at144.Update(input);
    }
    Assert::AreEqual(0.02f - 0.06f * 0.2f, at30.ShakeAmplitude(), 1.0e-5f, L"0.06 a second");
    Assert::AreEqual(at30.ShakeAmplitude(), at144.ShakeAmplitude(), 2.0e-4f);
    Run(at30, input, 1.0f);
    Assert::AreEqual(0.0f, at30.ShakeAmplitude());
  }
};

} // namespace NeuronClientTests
