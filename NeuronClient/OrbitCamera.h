#pragma once

#include <DirectXMath.h>

namespace Neuron
{

/// Where an orbit camera is: the point it looks at, the direction it looks from, and how far away.
/// Pitch is elevation above the target's horizon: positive looks down. Yaw 0 looks along +Z.
struct CameraPose
{
  DirectX::XMFLOAT3 target;
  float yawDegrees;
  float pitchDegrees;
  float distance;
  float fovYDegrees;
};

/// An orbit / pan / zoom camera. Pure math with no GPU state, so it is unit-testable.
class OrbitCamera
{
public:
  static constexpr float NEAR_Z = 1.0f;
  static constexpr float FAR_Z = 8000.0f;
  static constexpr float MIN_DISTANCE = 25.0f;
  static constexpr float MAX_DISTANCE = 2500.0f;
  static constexpr float MAX_PITCH_DEGREES = 85.0f;
  static constexpr float ORBIT_DEGREES_PER_PIXEL = 0.25f;
  static constexpr float ZOOM_FACTOR_PER_STEP = 0.88f;

  void SetPose(const CameraPose& _pose) noexcept;
  [[nodiscard]] const CameraPose& Pose() const noexcept
  {
    return m_pose;
  }

  void Orbit(float _deltaXPixels, float _deltaYPixels) noexcept;

  /// Moves the target in the view plane so that the point under the cursor follows the cursor.
  void Pan(float _deltaXPixels, float _deltaYPixels, float _viewportHeightPixels) noexcept;

  /// Positive steps move closer.
  void Zoom(float _wheelSteps) noexcept;

  [[nodiscard]] DirectX::XMFLOAT3 Forward() const noexcept;
  [[nodiscard]] DirectX::XMFLOAT3 Position() const noexcept;
  [[nodiscard]] DirectX::XMMATRIX View() const noexcept;
  [[nodiscard]] DirectX::XMMATRIX Projection(float _aspect) const noexcept;

private:
  CameraPose m_pose{{0.0f, 0.0f, 0.0f}, 0.0f, 30.0f, 300.0f, 40.0f};
};

} // namespace Neuron
