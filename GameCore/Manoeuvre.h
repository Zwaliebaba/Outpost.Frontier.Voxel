#pragma once

#include "FlightModel.h"
#include "UniverseObject.h"

#include <cstdint>

namespace Outpost
{

enum class ManoeuvreKind : std::uint8_t
{
  FreeFlight,   // the player's wishes, through the arbiter
  HoldPosition, // stay where the hold began
  GoTo          // fly to a point and stop short of it
};

/// What a ship is doing, as the manoeuvre layer of the blueprint (ch. 02 §3.9, ch. 05) has it: the
/// player's ship and every NPC are driven through the same layer, and an automatic manoeuvre
/// replaces free flight with nothing changed below. To go to an object, the caller sets the
/// target to the object's position each tick.
struct Manoeuvre
{
  ManoeuvreKind kind = ManoeuvreKind::FreeFlight;
  UniversePosition target;     // hold: the anchor; go-to: the point
  float standOffMetres = 0.0f; // go-to: where to stop, measured from the target

  [[nodiscard]] static Manoeuvre FreeFlight() noexcept
  {
    return {};
  }
  [[nodiscard]] static Manoeuvre Hold(const UniversePosition& _anchor) noexcept
  {
    return Manoeuvre{ManoeuvreKind::HoldPosition, _anchor, 0.0f};
  }
  [[nodiscard]] static Manoeuvre GoTo(const UniversePosition& _target, float _standOffMetres) noexcept
  {
    return Manoeuvre{ManoeuvreKind::GoTo, _target, _standOffMetres};
  }

  friend bool operator==(const Manoeuvre&, const Manoeuvre&) = default;
};

// A go-to has arrived once it is within this of its stand-off and nearly still.
inline constexpr float ARRIVAL_TOLERANCE_METRES = 5.0f;
inline constexpr float ARRIVAL_SPEED_METRES_PER_SECOND = 2.0f;
// A hold lets drag settle the ship inside this distance of its anchor, and flies back from outside it.
inline constexpr float HOLD_TOLERANCE_METRES = 10.0f;

struct ManoeuvreOutput
{
  FlightControls controls;
  bool arrived = false; // go-to: at the stand-off and still; hold: inside its tolerance
};

/// The precedence rules of ch. 02 §3.8 and §3.9: reverse beats throttle, a killed engine beats
/// cruise, and cruise is refused while the way ahead is obstructed (task 1.11, ShipFlight.h).
[[nodiscard]] FlightControls Arbitrate(const FlightControls& _wishes, bool _wayAheadObstructed = false) noexcept;

/// One tick of a manoeuvre: what it asks of the ship, through the arbiter. _input is the player's
/// wishes and matters only to free flight. Pure: the same arguments give the same controls.
[[nodiscard]] ManoeuvreOutput RunManoeuvre(const Manoeuvre& _manoeuvre, const FlightControls& _input, const ShipFlightSpec& _spec,
                                           const FlightState& _state, bool _wayAheadObstructed = false) noexcept;

} // namespace Outpost
