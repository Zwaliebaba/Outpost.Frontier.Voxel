#pragma once

#include "LoopbackTransport.h"
#include "World.h"

#include <string>

namespace Outpost
{

/// The server's side of what a client asks (plan task 1.4, AGENTS.md R19): every message waiting
/// from the client, decoded and handed to the world, which decides whether to act on it. A command
/// for a ship the session does not fly is refused by the world and costs nothing. False, with the
/// reason, if a message did not decode or is a kind no client sends: a defect, never a normal event.
[[nodiscard]] bool ReceiveCommands(Neuron::LoopbackTransport& _transport, World& _world, std::string& _outError);

} // namespace Outpost
