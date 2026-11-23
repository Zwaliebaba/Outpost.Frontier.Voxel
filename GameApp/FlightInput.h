#pragma once

#include "CommandMap.h"
#include "FlightModel.h"
#include "ShipFlight.h"

#include <DirectXMath.h>

#include <cstdint>

namespace Outpost
{

/// Turns the player's commands and cursor into the ShipCommand for each tick (blueprint ch. 09
/// §3.2-3.5; plan task 1.6). It is sampled once per tick rather than once per frame, so a held key
/// or a hold threshold means the same at any frame rate.
///
/// - **Mouse flight:** while MouseFlight is held, or after MouseFlightLock toggles it on, the cursor's
///   offset from the centre of the screen is a steering demand: half the screen's width or height
///   is full deflection, times TURN_SENSITIVITY. A press arms steering only once it has been held
///   for HOLD_SECONDS or the cursor has left a radius of a sixteenth of the screen's width, so that
///   a click does not jerk the ship.
/// - **Banking:** with no roll key held the ship banks into the turn, to maxBank times the yaw demand
///   times the throttle (never less than BANK_THROTTLE_FLOOR of it), and levels to the camera's up
///   when the turn ends. The roll demand closes the gap at full deflection per BANK_GAIN_DEGREES.
/// - **Throttle:** 0 to 1, moved at THROTTLE_PER_SECOND by the throttle keys and by THROTTLE_STEP a
///   wheel notch. Throttling down, or reaching 0, drops cruise; engaging cruise sets full throttle.
/// - **Orders:** a go-to from the tactical view is flown until it is cancelled, or until the player
///   steers or moves the throttle, which returns the ship to free flight.
/// - **Fire:** while Fire is held the guns fire at the aim point, which the client sets from the
///   cursor (plan task 3.4; GameDesign.md D22).
class FlightInput
{
public:
  static constexpr float TURN_SENSITIVITY = 1.0f;
  static constexpr float HOLD_SECONDS = 0.25f;
  static constexpr float CLICK_RADIUS_SHARE_OF_WIDTH = 1.0f / 16.0f;
  static constexpr float THROTTLE_PER_SECOND = 1.0f;
  static constexpr float THROTTLE_STEP = 0.2f;
  static constexpr float BANK_THROTTLE_FLOOR = 0.4f;
  static constexpr float BANK_GAIN_DEGREES = 15.0f;

  /// Finds the commands this layer reads; a bindings file without one simply never sends it.
  void Bind(const Neuron::CommandMap& _commands);
  /// Toggles and steps: the presses of commands that are not held.
  void OnCommand(const Neuron::CommandEvent& _event);
  void SetCursor(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels) noexcept;

  /// Orders the ship to a point, stopping a distance short of it.
  void GoTo(const UniversePosition& _target, float _standOffMetres) noexcept;
  /// Back to free flight: drops an order and the mouse lock.
  void Cancel() noexcept;
  /// The arbiter refused cruise: the wish is withdrawn rather than retried every tick.
  void RefuseCruise() noexcept;
  /// Whether the cursor may steer: not while the tactical view has it.
  void SetMouseEnabled(bool _enabled) noexcept;

  /// The command for the next tick. _referenceUp is the camera's up, which banking levels to.
  [[nodiscard]] ShipCommand Sample(const Neuron::CommandMap& _commands, const FlightState& _predicted,
                                   const DirectX::XMFLOAT3& _referenceUp, float _maxBankDegrees, std::uint32_t _ticksPerSecond);

  [[nodiscard]] float Throttle() const noexcept
  {
    return m_throttle;
  }
  [[nodiscard]] bool CruiseWish() const noexcept
  {
    return m_cruise;
  }
  [[nodiscard]] bool EngineOn() const noexcept
  {
    return m_engineOn;
  }
  [[nodiscard]] bool MouseLocked() const noexcept
  {
    return m_locked;
  }
  /// Whether the cursor steers the ship this tick.
  [[nodiscard]] bool MouseSteering() const noexcept
  {
    return m_mouseEnabled && (m_locked || m_armed);
  }
  [[nodiscard]] const Manoeuvre& Order() const noexcept
  {
    return m_order;
  }
  /// The cursor's steering demand, -1..1 per axis: x right, y down.
  [[nodiscard]] DirectX::XMFLOAT2 Demand() const noexcept;
  [[nodiscard]] DirectX::XMFLOAT2 Cursor() const noexcept
  {
    return DirectX::XMFLOAT2{m_cursorX, m_cursorY};
  }

  /// The demand a cursor makes on a screen: its offset from the centre over half the screen.
  [[nodiscard]] static DirectX::XMFLOAT2 CursorDemand(float _x, float _y, float _width, float _height) noexcept;
  /// The bank a turn aims for, in degrees; negative, the right wing down, for a right turn.
  [[nodiscard]] static float BankTarget(float _maxBankDegrees, float _yawDemand, float _throttle) noexcept;
  /// The ship's bank against an up direction, in degrees; positive with the right wing up.
  [[nodiscard]] static float BankOf(const DirectX::XMFLOAT4& _orientation, const DirectX::XMFLOAT3& _up) noexcept;

private:
  struct Ids
  {
    Neuron::CommandId throttleUp = Neuron::NO_COMMAND;
    Neuron::CommandId throttleDown = Neuron::NO_COMMAND;
    Neuron::CommandId throttleStepUp = Neuron::NO_COMMAND;
    Neuron::CommandId throttleStepDown = Neuron::NO_COMMAND;
    Neuron::CommandId reverse = Neuron::NO_COMMAND;
    Neuron::CommandId engineKill = Neuron::NO_COMMAND;
    Neuron::CommandId cruise = Neuron::NO_COMMAND;
    Neuron::CommandId thruster = Neuron::NO_COMMAND;
    Neuron::CommandId strafeLeft = Neuron::NO_COMMAND;
    Neuron::CommandId strafeRight = Neuron::NO_COMMAND;
    Neuron::CommandId strafeUp = Neuron::NO_COMMAND;
    Neuron::CommandId strafeDown = Neuron::NO_COMMAND;
    Neuron::CommandId rollLeft = Neuron::NO_COMMAND;
    Neuron::CommandId rollRight = Neuron::NO_COMMAND;
    Neuron::CommandId noseUp = Neuron::NO_COMMAND;
    Neuron::CommandId noseDown = Neuron::NO_COMMAND;
    Neuron::CommandId yawLeft = Neuron::NO_COMMAND;
    Neuron::CommandId yawRight = Neuron::NO_COMMAND;
    Neuron::CommandId mouseFlight = Neuron::NO_COMMAND;
    Neuron::CommandId mouseFlightLock = Neuron::NO_COMMAND;
    Neuron::CommandId cancel = Neuron::NO_COMMAND;
    Neuron::CommandId fire = Neuron::NO_COMMAND;
  };

  void SetThrottle(float _throttle) noexcept;

  Ids m_ids;
  Manoeuvre m_order;
  float m_throttle = 0.0f;
  float m_cursorX = 0.0f;
  float m_cursorY = 0.0f;
  float m_width = 1.0f;
  float m_height = 1.0f;
  float m_pressX = 0.0f;
  float m_pressY = 0.0f;
  std::uint32_t m_pressTicks = 0;
  bool m_pressed = false;
  bool m_armed = false;
  bool m_locked = false;
  bool m_cruise = false;
  bool m_engineOn = true;
  bool m_mouseEnabled = true;
};

} // namespace Outpost
