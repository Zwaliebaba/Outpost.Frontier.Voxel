#pragma once

#include "ShipArchetype.h"
#include "WorldObject.h"

#include <DirectXMath.h>

#include <cstdint>

namespace Outpost
{

// The global constants of the blueprint's flight model (ch. 02 §3.7, §3.8, §4).
inline constexpr float CRUISE_SPEED_METRES_PER_SECOND = 300.0f;
inline constexpr float CRUISE_RAMP_SECONDS = 5.0f;
inline constexpr float CRUISE_EXTRA_DRAG_KILOGRAMS_PER_SECOND = 3.0f;
inline constexpr float CRUISE_DISRUPTION_COOLDOWN_SECONDS = 5.0f;
inline constexpr float MAX_SPEED_METRES_PER_SECOND = 1000.0f;
inline constexpr float MAX_ROTATION_RADIANS_PER_TICK = 0.94f;

/// Everything the flight model needs about one ship: its hull and equipment, resolved from
/// content and flattened. Per-axis vectors are pitch (X), yaw (Y), roll (Z) in the ship's frame.
struct ShipFlightSpec
{
  float massKilograms = 1.0f;
  DirectX::XMFLOAT3 inertiaKilogramSquareMetres{1.0f, 1.0f, 1.0f};
  float hullDragKilogramsPerSecond = 0.0f;
  DirectX::XMFLOAT3 steeringTorqueNewtonMetres{};
  DirectX::XMFLOAT3 angularDragNewtonMetreSeconds{1.0f, 1.0f, 1.0f};
  float strafeForceNewtons = 0.0f;
  float strafeDrainPerSecond = 0.0f;
  float engineThrustNewtons = 0.0f;
  float engineDragKilogramsPerSecond = 0.0f;
  float reverseFraction = 0.0f;
  float cruiseChargeSeconds = 0.0f;
  float cruisePowerPerSecond = 0.0f;
  float powerCapacity = 0.0f;
  float powerChargePerSecond = 0.0f;
  float thrustCapacity = 0.0f;
  float thrustChargePerSecond = 0.0f;
  float thrusterForceNewtons = 0.0f;
  float thrusterDrainPerSecond = 0.0f;

  /// Speed at full throttle with the engine on and cruise off: thrust over drag.
  [[nodiscard]] float TopSpeedMetresPerSecond() const noexcept;
};

/// The flattened spec of a ship archetype; its references must resolve (ParseShipContent does that).
[[nodiscard]] ShipFlightSpec FlightSpecFor(const ShipContent& _content, const ShipArchetype& _ship);

enum class StrafeDirection : std::uint8_t
{
  None,
  Left,
  Right,
  Up,
  Down
};

enum class CruiseState : std::uint8_t
{
  Off,
  Charging,
  Cruising,
  Cooldown
};

/// What a ship is told to do for one tick, after the manoeuvre layer's arbiter (Manoeuvre.h).
struct FlightControls
{
  float throttle = 0.0f;        // -1..1; the input layer forces 1 when cruise engages (ch. 02 §3.8)
  DirectX::XMFLOAT3 steering{}; // axis throttle per ship axis, -1..1
  StrafeDirection strafe = StrafeDirection::None;
  bool reverse = false; // commands throttle -1; the arbiter folds it in
  bool thruster = false;
  bool engineOn = true; // false is engine kill
  bool cruise = false;  // the wish to be in cruise

  friend bool operator==(const FlightControls& _a, const FlightControls& _b) noexcept
  {
    return _a.throttle == _b.throttle && _a.steering.x == _b.steering.x && _a.steering.y == _b.steering.y &&
           _a.steering.z == _b.steering.z && _a.strafe == _b.strafe && _a.reverse == _b.reverse && _a.thruster == _b.thruster &&
           _a.engineOn == _b.engineOn && _a.cruise == _b.cruise;
  }
};

/// One ship's flight state: its pose and motion, its pools and its engine's modes.
struct FlightState
{
  WorldPosition position;
  DirectX::XMFLOAT4 orientation{0.0f, 0.0f, 0.0f, 1.0f}; // ship frame to world
  DirectX::XMFLOAT3 velocityMetresPerSecond{};           // world frame
  DirectX::XMFLOAT3 angularVelocityRadiansPerSecond{};   // ship frame
  float powerPool = 0.0f;
  float thrustPool = 0.0f;
  bool thrusterActive = false;
  CruiseState cruise = CruiseState::Off;
  // Charging counts up to the charge time; a cooldown counts up from minus the cooldown to 0, and
  // charging may begin while it is still negative (ch. 02 §3.8).
  std::int32_t cruiseTimerTicks = 0;
  float cruiseRamp = 0.0f; // r: the share of cruise speed that cruise thrust aims for

  [[nodiscard]] float SpeedMetresPerSecond() const noexcept;

  /// Exact equality, bit for bit in effect: what prediction and the server are tested against.
  friend bool operator==(const FlightState& _a, const FlightState& _b) noexcept;
};

/// A ship at rest at a pose, with full pools.
[[nodiscard]] FlightState RestingFlightState(const ShipFlightSpec& _spec, const Pose& _pose) noexcept;

/// The blueprint's flight model on a fixed tick (ch. 02 §3.7 and §3.8), shared by both halves.
///
/// Drag dominates: velocity relaxes towards force over drag with the time constant mass over
/// drag, so top speed is thrust over drag and mass only sets how quickly it is reached. Each tick
/// integrates that relaxation exactly, in its exponential form, with the forces held for the tick:
/// a velocity v under force F with drag D moves to F/D + (v - F/D) e^(-D dt / m), and the position
/// by the integral of that. The same holds per axis for rotation. The tick length is the caller's,
/// as ticks a second, so that both halves step identically (ADR-007).
///
/// Order within a tick: the pools recharge; the engine's modes advance; forces and torques are
/// gathered and paid for; velocities relax; the global limits apply; the pose advances.
void StepFlight(const ShipFlightSpec& _spec, const FlightControls& _controls, std::uint32_t _ticksPerSecond, FlightState& _state);

/// A disrupting hit (ch. 03): charging or cruising ends, and cruise cannot charge again until the
/// cooldown has run.
void DisruptCruise(std::uint32_t _ticksPerSecond, FlightState& _state) noexcept;

/// The ship's nose (+Z, as every ship model faces: ADR-002) in the world frame.
[[nodiscard]] DirectX::XMVECTOR NoseDirection(const FlightState& _state) noexcept;

} // namespace Outpost
