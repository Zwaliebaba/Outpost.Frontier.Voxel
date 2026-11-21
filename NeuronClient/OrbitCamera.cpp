#include "OrbitCamera.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Neuron
{

void OrbitCamera::SetPose(const CameraPose& _pose) noexcept
{
  m_pose = _pose;
  m_pose.pitchDegrees = std::clamp(m_pose.pitchDegrees, -MAX_PITCH_DEGREES, MAX_PITCH_DEGREES);
  m_pose.distance = std::clamp(m_pose.distance, MIN_DISTANCE, MAX_DISTANCE);
}

void OrbitCamera::Orbit(float _deltaXPixels, float _deltaYPixels) noexcept
{
  m_pose.yawDegrees = std::fmod(m_pose.yawDegrees + _deltaXPixels * ORBIT_DEGREES_PER_PIXEL, 360.0f);
  m_pose.pitchDegrees = std::clamp(m_pose.pitchDegrees + _deltaYPixels * ORBIT_DEGREES_PER_PIXEL, -MAX_PITCH_DEGREES, MAX_PITCH_DEGREES);
}

void OrbitCamera::Pan(float _deltaXPixels, float _deltaYPixels, float _viewportHeightPixels) noexcept
{
  if (_viewportHeightPixels <= 0.0f)
  {
    return;
  }
  // World units per pixel at the target's distance.
  const float unitsPerPixel = 2.0f * m_pose.distance * std::tan(XMConvertToRadians(m_pose.fovYDegrees) * 0.5f) / _viewportHeightPixels;
  const XMFLOAT3 forwardValue = Forward();
  const XMVECTOR forward = XMLoadFloat3(&forwardValue);
  const XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), forward));
  const XMVECTOR up = XMVector3Cross(forward, right);
  XMVECTOR target = XMLoadFloat3(&m_pose.target);
  target = XMVectorAdd(target, XMVectorScale(right, -_deltaXPixels * unitsPerPixel));
  target = XMVectorAdd(target, XMVectorScale(up, _deltaYPixels * unitsPerPixel));
  XMStoreFloat3(&m_pose.target, target);
}

void OrbitCamera::Zoom(float _wheelSteps) noexcept
{
  m_pose.distance = std::clamp(m_pose.distance * std::pow(ZOOM_FACTOR_PER_STEP, _wheelSteps), MIN_DISTANCE, MAX_DISTANCE);
}

XMFLOAT3 OrbitCamera::Forward() const noexcept
{
  const float yaw = XMConvertToRadians(m_pose.yawDegrees);
  const float pitch = XMConvertToRadians(m_pose.pitchDegrees);
  return XMFLOAT3{std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

XMFLOAT3 OrbitCamera::Position() const noexcept
{
  const XMFLOAT3 forward = Forward();
  return XMFLOAT3{m_pose.target.x - forward.x * m_pose.distance, m_pose.target.y - forward.y * m_pose.distance,
                  m_pose.target.z - forward.z * m_pose.distance};
}

XMMATRIX OrbitCamera::View() const noexcept
{
  const XMFLOAT3 position = Position();
  return XMMatrixLookAtLH(XMLoadFloat3(&position), XMLoadFloat3(&m_pose.target), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

XMMATRIX OrbitCamera::Projection(float _aspect) const noexcept
{
  return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_pose.fovYDegrees), _aspect, NEAR_Z, FAR_Z);
}

} // namespace Neuron
