#pragma once

#include "LoopbackTransport.h"
#include "Messages.h"
#include "World.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Outpost
{

/// One client session as the server sees it (AGENTS.md R25, ADR-008): what that client has been
/// told, and the messages that bring it up to date. Every object reaches the client as its own
/// messages; no message carries the whole world. In single player there is one observer and it
/// passes every object. Interest, priority and byte budgets are added behind Publish in slice 10,
/// without changing its callers.
class Observer
{
public:
  /// Encodes what changed in the world since the last call and sends it to the client. It reads
  /// the world and never changes it. False if a message could not be encoded or sent, which is a
  /// defect: nothing the world holds should be unsendable.
  [[nodiscard]] bool Publish(const World& _world, Neuron::LoopbackTransport& _transport);

  /// The ship this session flies. The next Publish tells the client, with the ship's whole flight
  /// state, and Publish tells it again after any tick whose state it could not have predicted.
  void AssignShip(ObjectId _ship) noexcept
  {
    m_ownShip = _ship;
    m_ownShipSentTick.reset();
  }

  /// Messages sent by the last Publish, for tests and the smoke report.
  [[nodiscard]] std::size_t LastMessageCount() const noexcept
  {
    return m_lastMessageCount;
  }

private:
  [[nodiscard]] bool Send(const Message& _message, Neuron::LoopbackTransport& _transport);

  std::optional<std::uint64_t> m_sentTick;
  std::vector<WorldObject> m_knownObjects; // as the client was last told
  std::vector<ShipState> m_sentShips;
  std::vector<ObjectiveState> m_sentObjectives;
  std::vector<ResourceState> m_sentResources;
  std::vector<ShipMotionState> m_sentMotions;
  ObjectId m_ownShip = NO_OBJECT;
  std::optional<std::uint64_t> m_ownShipSentTick;
  std::size_t m_lastMessageCount = 0;
};

} // namespace Outpost
