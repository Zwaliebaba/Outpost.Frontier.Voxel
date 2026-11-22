#pragma once

#include "Collision.h"
#include "FlightModel.h"
#include "Manoeuvre.h"

#include <cstdint>
#include <span>

namespace Outpost
{

// Cruise is refused while anything lies within this distance along the nose (ch. 02 §3.9): two
// seconds at cruise speed.
inline constexpr float CRUISE_CLEARANCE_METRES = 2.0f * CRUISE_SPEED_METRES_PER_SECOND;
// A ship that meets a body stops this far short of it, so the next tick does not start touching.
inline constexpr float CONTACT_STANDOFF_METRES = 0.01f;

/// What one ship is told for one tick: the manoeuvre it runs and the wishes free flight passes on.
/// The player's command carries both (FlightInputMessage); an NPC's wishes are empty.
struct ShipCommand
{
  Manoeuvre manoeuvre;
  FlightControls wishes;

  friend bool operator==(const ShipCommand&, const ShipCommand&) = default;
};

/// A ship as the tick sees it: its flight spec and the sphere it sweeps as.
struct ShipBody
{
  ObjectId object = NO_OBJECT;
  ShipFlightSpec spec;
  float radiusMetres = 1.0f;
};

/// What a tick did to a ship.
struct ShipTickResult
{
  FlightControls controls; // after the manoeuvre layer and its arbiter
  bool arrived = false;
  bool obstructed = false;
  bool contact = false;
  ObjectId contactObject = NO_OBJECT;
  bool contactMoves = false;                // the body met moves, which the client's prediction cannot know
  float contactSpeedMetresPerSecond = 0.0f; // relative speed at the moment of contact
};

/// One tick of one ship, the same on both halves (plan tasks 1.4, 1.7 and 1.11). The manoeuvre layer
/// turns the command into controls, refusing cruise when an immovable body lies within
/// CRUISE_CLEARANCE_METRES ahead; the flight model integrates them; and the tick's motion is swept against the bodies. At
/// the first contact the ship stops short of the body and bounces, and the rest of the tick's motion
/// is dropped, so that no speed carries a ship through a wall. The server runs it for every ship
/// against every body; the client runs it for its own ship against the static bodies alone.
/// The body a flying object sweeps as: its archetype's flight spec, and a sphere around its box
/// measured from its pivot, so that it holds whatever way the ship turns. The server and the
/// client's prediction both build it here, so they sweep the same sphere. _outLocalBox, when given,
/// receives the box in the ship's own frame, its centre an offset from the pivot.
[[nodiscard]] ShipBody ShipBodyFor(const SceneDefinition& _scene, const ShipContent& _content, const ShipArchetype& _archetype,
                                   const UniverseObject& _object, CollisionBox* _outLocalBox = nullptr);

[[nodiscard]] ShipTickResult StepShip(const ShipBody& _ship, const ShipCommand& _command, std::span<const CollisionBox> _bodies,
                                      std::uint32_t _ticksPerSecond, FlightState& _state) noexcept;

} // namespace Outpost
