#include "ChaseCamera.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Neuron
{

namespace
{

[[nodiscard]] XMVECTOR Rotate(float _x, float _y, float _z, FXMVECTOR _orientation) noexcept
{
  return XMVector3Rotate(XMVectorSet(_x, _y, _z, 0.0f), _orientation);
}

/// _up made perpendicular to _forward, or _fallback's part perpendicular to it when _up is parallel.
[[nodiscard]] XMVECTOR Orthogonal(FXMVECTOR _up, FXMVECTOR _forward, FXMVECTOR _fallback) noexcept
{
  XMVECTOR up = XMVectorSubtract(_up, XMVectorScale(_forward, XMVectorGetX(XMVector3Dot(_up, _forward))));
  if (XMVectorGetX(XMVector3LengthSq(up)) < 1.0e-8f)
  {
    up = XMVectorSubtract(_fallback, XMVectorScale(_forward, XMVectorGetX(XMVector3Dot(_fallback, _forward))));
  }
  return XMVector3Normalize(up);
}

} // namespace

void ChaseCamera::Reset(const ChaseCameraDesc& _desc, const XMFLOAT4& _shipOrientation) noexcept
{
  m_desc = _desc;
  const XMVECTOR orientation = XMLoadFloat4(&_shipOrientation);
  XMStoreFloat3(&m_forward, Rotate(0.0f, 0.0f, 1.0f, orientation));
  XMStoreFloat3(&m_up, Rotate(0.0f, 1.0f, 0.0f, orientation));
  m_angularVelocity = XMFLOAT3{};
  m_smoothedSpeed = 0.0f;
  m_backMetres = _desc.backMetres;
  m_shakeRadians = 0.0f;
  m_shakeAngles = XMFLOAT3{};
}

void ChaseCamera::Update(const ChaseInput& _input) noexcept
{
  const float dt = std::max(0.0f, _input.frameSeconds);
  const XMVECTOR ship = XMLoadFloat4(&_input.shipOrientation);
  const XMVECTOR shipUp = Rotate(0.0f, 1.0f, 0.0f, ship);

  // Turn look-ahead: the nose turned further into the turn, in the ship's own frame.
  const float yaw = XMConvertToRadians(m_desc.lookSideDegrees) * std::clamp(_input.yawThrottle, -1.0f, 1.0f);
  const float pitchThrottle = std::clamp(_input.pitchThrottle, -1.0f, 1.0f);
  const float pitch = XMConvertToRadians(pitchThrottle > 0.0f ? m_desc.lookDownDegrees : m_desc.lookUpDegrees) * pitchThrottle;
  const XMVECTOR target = XMVector3Rotate(Rotate(0.0f, 0.0f, 1.0f, XMQuaternionRotationRollPitchYaw(pitch, yaw, 0.0f)), ship);

  // Swing towards it at a rate that responds to the angle left, its change limited by the camera's
  // angular acceleration.
  XMVECTOR forward = XMLoadFloat3(&m_forward);
  XMVECTOR up = XMLoadFloat3(&m_up);
  XMVECTOR axis = XMVector3Cross(forward, target);
  const float sine = XMVectorGetX(XMVector3Length(axis));
  const float cosine = XMVectorGetX(XMVector3Dot(forward, target));
  XMVECTOR desired = XMVectorZero();
  if (sine > 1.0e-6f || cosine < 0.0f)
  {
    if (sine <= 1.0e-6f)
    {
      axis = up; // directly behind: turn about the camera's up
    }
    desired = XMVectorScale(XMVector3Normalize(axis), std::atan2(sine, cosine) * TURN_RESPONSE_PER_SECOND);
  }
  XMVECTOR omega = XMLoadFloat3(&m_angularVelocity);
  XMVECTOR change = XMVectorSubtract(desired, omega);
  const float changeLength = XMVectorGetX(XMVector3Length(change));
  const float changeLimit = MAX_TURN_ACCELERATION * dt;
  if (changeLength > changeLimit)
  {
    change = XMVectorScale(change, changeLimit / changeLength);
  }
  omega = XMVectorAdd(omega, change);
  XMStoreFloat3(&m_angularVelocity, omega);
  const float turn = XMVectorGetX(XMVector3Length(omega)) * dt;
  if (turn > 0.0f)
  {
    // The up turns with the forward, so that turning never rolls the view by itself.
    const XMVECTOR rotation = XMQuaternionRotationNormal(XMVector3Normalize(omega), turn);
    forward = XMVector3Normalize(XMVector3Rotate(forward, rotation));
    up = XMVector3Rotate(up, rotation);
  }

  // Levelling: the up settles onto the ship's only while the ship is not turning.
  if (std::abs(_input.yawThrottle) < LEVEL_DEMAND_BELOW)
  {
    const float share = 1.0f - std::exp(-dt / LEVEL_SECONDS);
    up = XMVectorAdd(up, XMVectorScale(XMVectorSubtract(shipUp, up), share));
  }
  up = Orthogonal(up, forward, shipUp);
  XMStoreFloat3(&m_forward, forward);
  XMStoreFloat3(&m_up, up);

  // Speed lag.
  const float speed = XMVectorGetX(XMVector3Length(XMLoadFloat3(&_input.shipVelocityMetresPerSecond)));
  m_smoothedSpeed += (speed - m_smoothedSpeed) * (1.0f - std::exp(-dt / SPEED_SMOOTHING_SECONDS));
  m_backMetres = m_desc.backMetres + SPEED_LAG_SECONDS * m_smoothedSpeed + SLACK_SECONDS * std::max(0.0f, speed - m_smoothedSpeed);

  // Shake.
  if (m_shakeRadians > 0.0f)
  {
    m_shakeAngles = XMFLOAT3{m_shakeRandom.Range(-m_shakeRadians, m_shakeRadians), m_shakeRandom.Range(-m_shakeRadians, m_shakeRadians),
                             m_shakeRandom.Range(-m_shakeRadians, m_shakeRadians)};
    m_shakeRadians = std::max(0.0f, m_shakeRadians - SHAKE_DECAY_PER_SECOND * dt);
  }
  else
  {
    m_shakeAngles = XMFLOAT3{};
  }
}

void ChaseCamera::Shake(float _amplitudeRadians) noexcept
{
  m_shakeRadians = std::max(m_shakeRadians, _amplitudeRadians);
}

XMFLOAT3 ChaseCamera::OffsetFromShip() const noexcept
{
  XMFLOAT3 offset{};
  XMStoreFloat3(
    &offset, XMVectorSubtract(XMVectorScale(XMLoadFloat3(&m_up), m_desc.upMetres), XMVectorScale(XMLoadFloat3(&m_forward), m_backMetres)));
  return offset;
}

XMFLOAT3 ChaseCamera::Forward() const noexcept
{
  return m_forward;
}

XMFLOAT3 ChaseCamera::Up() const noexcept
{
  return m_up;
}

XMMATRIX ChaseCamera::View() const noexcept
{
  const XMVECTOR forward = XMLoadFloat3(&m_forward);
  const XMVECTOR up = XMLoadFloat3(&m_up);
  const XMVECTOR right = XMVector3Cross(up, forward);
  // The shake turns the view in its own frame: yaw about up, pitch about right, roll about forward.
  const XMMATRIX basis(right, up, forward, XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));
  const XMMATRIX shaken = XMMatrixRotationRollPitchYaw(m_shakeAngles.y, m_shakeAngles.x, m_shakeAngles.z) * basis;
  return XMMatrixLookToLH(XMVectorZero(), shaken.r[2], shaken.r[1]);
}

XMMATRIX ChaseCamera::Projection(float _aspect) const noexcept
{
  return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_desc.fovYDegrees), _aspect, NEAR_Z, FAR_Z);
}

} // namespace Neuron
