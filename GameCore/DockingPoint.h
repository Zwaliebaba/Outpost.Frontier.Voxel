#pragma once

#include "UniverseObject.h"

#include <DirectXMath.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Outpost
{

struct SceneDefinition;

// Docking points (plan task 2.2, blueprint ch. 04 §2). A docking point is a record on a model that
// names three of its `dock`-class sockets: the entry, where the approach begins outside the station;
// the approach point; and the pad, where the ship ends up. A docked ship sits with its pivot on the
// pad, its nose along the pad socket's outward normal and its top along the socket's up.

/// The kind of docking point, which is also the docking class a hull carries: a point accepts the
/// ship whose class is its kind (ch. 04 §2's table, without the jump and airlock kinds).
enum class DockKind : std::uint8_t
{
  Berth,  // an interior bay of a station
  Medium, // a medium mooring: the ship stays outside
  Large   // a large mooring
};

[[nodiscard]] std::string_view DockKindName(DockKind _kind) noexcept;
[[nodiscard]] std::optional<DockKind> ParseDockKind(std::string_view _text) noexcept;

/// Where a ship is in docking and launching, as the server decides it (ch. 04 §3.3 to §3.5).
enum class DockPhase : std::uint8_t
{
  None,        // flying free
  FarApproach, // flying to within FAR_APPROACH_METRES of the station
  Waiting,     // holding until a docking point is free
  Staging,     // flying to the docking point's entry
  Final,       // carried along the approach to the pad, collisions off
  Docked,      // in the berth, protected (GameDesign.md D16)
  Launching    // carried back out to the entry, collisions off and protected
};

/// Whether nothing may harm a ship in this phase (GameDesign.md D16): while it is carried in, docked,
/// or carried out. One rule for both halves: the server honours it, and the client's hazard warning
/// follows it.
[[nodiscard]] constexpr bool Protects(DockPhase _phase) noexcept
{
  return _phase == DockPhase::Final || _phase == DockPhase::Docked || _phase == DockPhase::Launching;
}

/// The server's answer to a docking or launch command (ch. 04 §3.2), or None for a change of phase.
enum class DockAnswer : std::uint8_t
{
  None,
  Proceed,
  Wait,
  CannotDock,   // no docking point of the station accepts the ship's class
  NoDock,       // the station has no docking points
  OutOfRange,   // no station within DOCK_RANGE_METRES has a point for the class (D20)
  TimedOut,     // not docked within the abandonment time of being cleared
  Canceled,     // the player cancelled
  LaunchRefused // the ship cannot fly: the reason says why
};

/// What a client asks of docking.
enum class DockCommand : std::uint8_t
{
  Dock,   // the Dock key: the nearest station in range (D20)
  Cancel, // leave the approach or the waiting list
  Launch
};

[[nodiscard]] std::string_view DockPhaseName(DockPhase _phase) noexcept;
[[nodiscard]] std::string_view DockAnswerName(DockAnswer _answer) noexcept;

/// The Dock key reaches this far (D20; ch. 04 §3.2).
inline constexpr float DOCK_RANGE_METRES = 10'000.0f;
/// The far approach flies to within this of the station before asking for a point (ch. 04 §3.3).
inline constexpr float FAR_APPROACH_METRES = 1'000.0f;

struct DockingPoint
{
  std::string name;
  DockKind kind = DockKind::Berth;
  std::string entry; // socket names on the same model
  std::string approach;
  std::string pad;
  float radiusMetres = 4.0f; // the ship is docked once its pivot is this close to the pad (ch. 04 §3.4)
};

/// A docking point in the universe: where its three sockets are, and how a docked ship is turned.
struct DockingPath
{
  UniversePosition entry;
  UniversePosition approach;
  UniversePosition pad;
  DirectX::XMFLOAT4 orientation{0.0f, 0.0f, 0.0f, 1.0f}; // a docked ship's, ship frame to world
  float radiusMetres = 4.0f;
};

/// The docking point's path for an instance of its model. The model's size must be set and the
/// sockets must exist (ValidateScene).
[[nodiscard]] DockingPath PathFor(const SceneDefinition& _scene, std::size_t _instanceIndex, const DockingPoint& _point) noexcept;

} // namespace Outpost
