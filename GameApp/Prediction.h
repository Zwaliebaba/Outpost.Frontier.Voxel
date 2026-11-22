#pragma once

#include "ShipFlight.h"
#include "UniverseSnapshot.h"

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace Outpost
{

/// The client's own ship, flown ahead of the server (plan task 1.7; AGENTS.md R19). Each tick the
/// client runs its command through StepShip, the function the server runs, against the static
/// bodies it was told of, and keeps the command until the server has run that tick. The server
/// sends its own state only when it differs: on assignment, and after a tick the client could not
/// foresee (a contact with a moving body, a command that did not arrive). Rebase then restarts from
/// that state and flies the remembered commands again, so the ship lands where the server has it.
class Prediction
{
public:
  /// Commands kept for ticks the server has not confirmed; older ones are dropped.
  static constexpr std::size_t MAX_HISTORY_TICKS = 240;

  /// Starts predicting a ship from the server's state at a tick.
  void Begin(const ShipBody& _body, std::uint64_t _tick, const FlightState& _state);
  void Stop() noexcept;
  [[nodiscard]] bool Active() const noexcept
  {
    return m_active;
  }
  /// The bodies the ship can meet that the client can foresee: the static ones.
  void SetBodies(std::vector<CollisionBox> _bodies);

  /// Flies the next tick on a command and remembers it.
  const ShipTickResult& Step(const ShipCommand& _command, std::uint32_t _ticksPerSecond);

  /// The server's state at a tick it ran. Returns true when it differed from the prediction for
  /// that tick and the ticks after were flown again from it.
  bool Rebase(std::uint64_t _tick, const FlightState& _state, std::uint32_t _ticksPerSecond);

  /// The last tick predicted.
  [[nodiscard]] std::uint64_t Tick() const noexcept
  {
    return m_tick;
  }
  [[nodiscard]] const FlightState& State() const noexcept
  {
    return m_state;
  }
  /// The state predicted for an earlier tick still in the history, or nullptr.
  [[nodiscard]] const FlightState* StateAt(std::uint64_t _tick) const noexcept;
  [[nodiscard]] const ShipTickResult& LastResult() const noexcept
  {
    return m_last;
  }
  [[nodiscard]] const ShipBody& Body() const noexcept
  {
    return m_body;
  }
  /// The pose a share of the way from the previous tick to the last one.
  [[nodiscard]] Pose Interpolated(float _alpha) const noexcept;
  /// How many corrections changed the prediction.
  [[nodiscard]] std::uint32_t Corrections() const noexcept
  {
    return m_corrections;
  }

private:
  struct Entry
  {
    std::uint64_t tick = 0;
    ShipCommand command;
    FlightState after; // the state the command flew the ship to
  };

  ShipBody m_body;
  std::vector<CollisionBox> m_bodies;
  std::deque<Entry> m_history;
  FlightState m_state;
  FlightState m_previous;
  ShipTickResult m_last;
  std::uint64_t m_tick = 0;
  std::uint32_t m_corrections = 0;
  bool m_active = false;
};

/// The poses of the objects the client does not fly, between the last two the server sent: the
/// client draws the universe a tick behind the latest so that every object moves smoothly between
/// ticks it has been told of, at any frame rate (plan task 1.7).
class PoseInterpolation
{
public:
  /// Takes in whatever poses arrived since the last call.
  void Update(const UniverseSnapshot& _universe);
  /// Where an object is at a fractional server tick, between its previous and its last pose.
  [[nodiscard]] Pose At(const UniverseObject& _object, double _tick) const noexcept;

private:
  struct Track
  {
    Pose previous;
    std::uint64_t previousTick = 0;
    Pose current;
    std::uint64_t currentTick = 0;
  };

  std::map<ObjectId, Track> m_tracks;
  std::uint64_t m_seenRevision = ~0ull;
};

/// A share of the way between two poses: the position in double precision, the orientation along
/// the shorter arc.
[[nodiscard]] Pose BlendPoses(const Pose& _from, const Pose& _to, double _share) noexcept;

} // namespace Outpost
