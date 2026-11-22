#pragma once

#include "Random.h"

#include <DirectXMath.h>

namespace Neuron
{

/// How a chase camera sits behind one hull: authored per hull (blueprint ch. 09 §3.9).
struct ChaseCameraDesc
{
  float upMetres = 6.0f;
  float backMetres = 25.0f;
  float lookSideDegrees = 20.0f; // look-ahead into a turn at full yaw throttle
  float lookUpDegrees = 7.0f;    // ... at full pitch-up throttle
  float lookDownDegrees = 25.0f; // ... at full pitch-down throttle
  float fovYDegrees = 60.0f;
};

/// What the camera follows in one frame. The camera works in offsets from the ship, and the game adds
/// the ship's double-precision position, so nothing here is far from the origin (ADR-007).
struct ChaseInput
{
  DirectX::XMFLOAT4 shipOrientation{0.0f, 0.0f, 0.0f, 1.0f}; // ship frame to world; the ship faces +Z
  DirectX::XMFLOAT3 shipVelocityMetresPerSecond{};
  float yawThrottle = 0.0f;   // -1..1, positive turning right
  float pitchThrottle = 0.0f; // -1..1, positive turning the nose down
  float frameSeconds = 0.0f;
};

/// The chase camera of the blueprint (ch. 09 §3.9; plan task 1.8). Pure math, beside OrbitCamera.
///
/// - **Offset:** up and back from the ship along the camera's own axes.
/// - **Speed lag:** the distance back grows by SPEED_LAG_SECONDS of travel at a smoothed copy of the
///   ship's speed, plus a slack that builds while the ship out-accelerates that copy, so that
///   acceleration shows as the ship pulling away.
/// - **Turn look-ahead:** the camera aims along the ship's nose turned further by the axis throttle
///   times the authored angles, and swings there at a turn rate limited by MAX_TURN_ACCELERATION.
/// - **Levelling:** the camera keeps its own up. Turning carries it along without rolling it, and it
///   settles onto the ship's up only while the ship is not turning, so a bank into a turn does not
///   roll the view. The game gives this up to the manoeuvre layer as the reference banking levels to.
/// - **Shake:** a small random rotation whose amplitude decays by SHAKE_DECAY_PER_SECOND.
class ChaseCamera
{
public:
  static constexpr float SPEED_LAG_SECONDS = 0.05f;
  static constexpr float SPEED_SMOOTHING_SECONDS = 0.4f;
  static constexpr float SLACK_SECONDS = 0.25f;
  static constexpr float TURN_RESPONSE_PER_SECOND = 8.0f;
  static constexpr float MAX_TURN_ACCELERATION = 30.0f; // radians per second squared
  static constexpr float LEVEL_SECONDS = 1.5f;
  static constexpr float LEVEL_DEMAND_BELOW = 0.05f;
  // The blueprint decays shake by 0.001 a frame at its 60 Hz: per second here, so that it is the
  // same at any frame rate.
  static constexpr float SHAKE_DECAY_PER_SECOND = 0.06f;
  static constexpr float NEAR_Z = 1.0f;
  static constexpr float FAR_Z = 20000.0f;

  /// Behind the ship, level with it, at rest.
  void Reset(const ChaseCameraDesc& _desc, const DirectX::XMFLOAT4& _shipOrientation) noexcept;
  void Update(const ChaseInput& _input) noexcept;
  /// Adds shake: 0.02 for a hull hit, 0.005 for a shield hit (ch. 09 §3.9).
  void Shake(float _amplitudeRadians) noexcept;

  /// Where the camera is, as an offset from the ship's position.
  [[nodiscard]] DirectX::XMFLOAT3 OffsetFromShip() const noexcept;
  [[nodiscard]] DirectX::XMFLOAT3 Forward() const noexcept;
  [[nodiscard]] DirectX::XMFLOAT3 Up() const noexcept;
  [[nodiscard]] float BackDistanceMetres() const noexcept
  {
    return m_backMetres;
  }
  [[nodiscard]] float ShakeAmplitude() const noexcept
  {
    return m_shakeRadians;
  }
  [[nodiscard]] const ChaseCameraDesc& Desc() const noexcept
  {
    return m_desc;
  }
  /// The view from the camera at the origin, shake included: rendering is relative to the camera.
  [[nodiscard]] DirectX::XMMATRIX View() const noexcept;
  [[nodiscard]] DirectX::XMMATRIX Projection(float _aspect) const noexcept;

private:
  ChaseCameraDesc m_desc;
  DirectX::XMFLOAT3 m_forward{0.0f, 0.0f, 1.0f};
  DirectX::XMFLOAT3 m_up{0.0f, 1.0f, 0.0f};
  DirectX::XMFLOAT3 m_angularVelocity{}; // radians per second, world frame
  float m_smoothedSpeed = 0.0f;
  float m_backMetres = 25.0f;
  float m_shakeRadians = 0.0f;
  DirectX::XMFLOAT3 m_shakeAngles{}; // yaw, pitch, roll of this frame's shake
  Pcg32 m_shakeRandom{0x5EEDu};
};

} // namespace Neuron
