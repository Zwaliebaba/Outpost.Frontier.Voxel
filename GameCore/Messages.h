#pragma once

#include "LoopbackTransport.h"
#include "ShipFlight.h"
#include "UniverseSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <variant>
#include <vector>

namespace Outpost
{

// The message catalogue (ADR-008): everything that crosses between client and server, one kind
// per message, encoded to bytes even in process so that nothing but data can cross (R18). Each
// kind has a fixed route: reliable on a named stream, or a datagram whose newest copy wins.

/// Raised whenever a message is added, removed or changes its encoding.
inline constexpr std::uint16_t CATALOGUE_VERSION = 4;

/// Every encoded message begins with: catalogue version (16 bits), kind (16 bits), payload length
/// in bytes (32 bits).
inline constexpr std::size_t MESSAGE_HEADER_BYTES = 8;

/// Reliable facts about the universe, in the order they happened.
inline constexpr std::uint16_t STREAM_UNIVERSE = 0;
/// What a client asks of the server.
inline constexpr std::uint16_t STREAM_COMMANDS = 1;

enum class MessageKind : std::uint16_t
{
  Tick = 1,
  Ship = 2,
  Objective = 3,
  Resource = 4,
  ObjectSpawn = 5,
  ObjectState = 6,
  FlightInput = 7,
  OwnShip = 8,
  ShipMotion = 9,
  DockCommand = 10,
  DockStatus = 11
};

/// The server's tick, so the client knows how current its picture is. Newest wins.
struct TickMessage
{
  std::uint64_t tick = 0;

  friend bool operator==(const TickMessage&, const TickMessage&) = default;
};

/// A ship's identity and health. A fact: reliable, on the universe stream.
struct ShipMessage
{
  ShipState ship;

  friend bool operator==(const ShipMessage&, const ShipMessage&) = default;
};

/// One objective, by its index in the list. A fact: reliable, on the universe stream.
struct ObjectiveMessage
{
  std::uint16_t index = 0;
  ObjectiveState objective;

  friend bool operator==(const ObjectiveMessage&, const ObjectiveMessage&) = default;
};

/// One resource, by its index, as of a tick. Changes every tick, so a datagram; the receiver keeps
/// the copy with the latest tick.
struct ResourceMessage
{
  std::uint64_t tick = 0;
  std::uint16_t index = 0;
  ResourceState resource;

  friend bool operator==(const ResourceMessage&, const ResourceMessage&) = default;
};

/// An object the client has not been told of: its identity, the content names it is presented by,
/// and its pose. A fact: reliable, on the universe stream. Later messages name it by id alone.
struct ObjectSpawnMessage
{
  UniverseObject object;

  friend bool operator==(const ObjectSpawnMessage&, const ObjectSpawnMessage&) = default;
};

/// An object's pose as of a tick. Changes whenever it moves, so a datagram; the receiver keeps the
/// copy with the latest tick for each object.
struct ObjectStateMessage
{
  std::uint64_t tick = 0;
  ObjectId id = NO_OBJECT;
  Pose pose;

  friend bool operator==(const ObjectStateMessage&, const ObjectStateMessage&) = default;
};

/// Client to server: the command for one tick of the ship the client flies, the manoeuvre and the
/// wishes the manoeuvre layer runs on (plan task 1.4). One per tick, stamped with the tick it is
/// for, so a datagram: the server runs each tick on the command stamped for it, or on the newest
/// before it when that one was lost.
struct FlightInputMessage
{
  std::uint64_t tick = 0;
  ObjectId ship = NO_OBJECT;
  ShipCommand command;
  friend bool operator==(const FlightInputMessage&, const FlightInputMessage&) = default;
};

/// Server to client: the ship this client flies, and its whole flight state as of a tick. Sent when
/// the session is given the ship, and again when the server's state has left what the client could
/// predict: a contact with a moving body, or a command that did not arrive (plan task 1.7). The
/// client predicts on from it. Reliable, on the universe stream.
struct OwnShipMessage
{
  std::uint64_t tick = 0;
  ObjectId ship = NO_OBJECT;
  FlightState state;
  friend bool operator==(const OwnShipMessage&, const OwnShipMessage&) = default;
};

/// Server to client: what a ship's engine is doing, for its plumes. Changes often, so a datagram;
/// the receiver keeps the copy with the latest tick for each ship.
struct ShipMotionMessage
{
  ShipMotionState motion;
  friend bool operator==(const ShipMotionMessage&, const ShipMotionMessage&) = default;
};

/// Client to server: dock at the nearest station, cancel the approach, or launch (plan tasks 2.3 and
/// 2.4). Reliable, on the commands stream: each one is acted on once, in order.
struct DockCommandMessage
{
  std::uint64_t tick = 0; // the client's tick when it asked
  ObjectId ship = NO_OBJECT;
  DockCommand command = DockCommand::Dock;

  friend bool operator==(const DockCommandMessage&, const DockCommandMessage&) = default;
};

/// Server to client: where the ship this client flies is in docking, and the answer to its last
/// command. Sent whenever either changes. While the phase is not None the server flies the ship and
/// the client draws it where the server says. Reliable, on the universe stream.
struct DockStatusMessage
{
  ObjectId ship = NO_OBJECT;
  DockStatusState status;

  friend bool operator==(const DockStatusMessage&, const DockStatusMessage&) = default;
};

using Message = std::variant<TickMessage, ShipMessage, ObjectiveMessage, ResourceMessage, ObjectSpawnMessage, ObjectStateMessage,
                             FlightInputMessage, OwnShipMessage, ShipMotionMessage, DockCommandMessage, DockStatusMessage>;

/// How a kind of message travels.
struct Route
{
  Neuron::Delivery delivery = Neuron::Delivery::Reliable;
  std::uint16_t stream = STREAM_UNIVERSE;

  friend bool operator==(const Route&, const Route&) = default;
};

enum class DecodeError : std::uint8_t
{
  Truncated,      // fewer bytes than the header or the payload length says
  WrongVersion,   // encoded against another catalogue
  UnknownKind,    // a kind this catalogue does not have
  LengthMismatch, // bytes beyond the stated payload length
  Malformed       // the payload does not read as its kind
};

[[nodiscard]] MessageKind KindOf(const Message& _message);
[[nodiscard]] Route RouteOf(MessageKind _kind) noexcept;

/// The message as bytes: header, then payload. Empty if a field cannot be encoded (a string
/// longer than the wire allows).
[[nodiscard]] std::vector<std::uint8_t> Encode(const Message& _message);

[[nodiscard]] std::expected<Message, DecodeError> Decode(std::span<const std::uint8_t> _bytes);

/// Applies a decoded message to the receiver's picture of the universe. Newest-wins messages older
/// than the copy held, and state for an object never spawned, change nothing.
void ApplyMessage(const Message& _message, UniverseSnapshot& _universe);

} // namespace Outpost
