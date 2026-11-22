#include "Commands.h"

#include "Messages.h"

#include <optional>
#include <variant>

namespace Outpost
{

bool ReceiveCommands(Neuron::LoopbackTransport& _transport, Universe& _universe, std::string& _outError)
{
  while (std::optional<Neuron::Packet> packet = _transport.Receive(Neuron::Peer::Server))
  {
    const auto message = Decode(packet->bytes);
    if (!message.has_value())
    {
      _outError = "a message from the client did not decode (error " + std::to_string(static_cast<int>(message.error())) + ")";
      return false;
    }
    if (const auto* dock = std::get_if<DockCommandMessage>(&*message))
    {
      static_cast<void>(_universe.Command(*dock));
      continue;
    }
    const auto* input = std::get_if<FlightInputMessage>(&*message);
    if (input == nullptr)
    {
      _outError =
        "the client sent a message of kind " + std::to_string(static_cast<int>(KindOf(*message))) + ", which only the server sends";
      return false;
    }
    static_cast<void>(_universe.SubmitCommand(*input));
  }
  return true;
}

} // namespace Outpost
