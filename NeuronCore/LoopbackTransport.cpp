#include "LoopbackTransport.h"

namespace Neuron
{

bool LoopbackTransport::Send(Peer _from, Delivery _delivery, std::uint16_t _stream, std::span<const std::uint8_t> _bytes)
{
  if (_delivery == Delivery::Datagram && _bytes.size() > MAX_DATAGRAM_BYTES)
  {
    return false;
  }
  std::deque<Packet>& queue = _from == Peer::Client ? m_toServer : m_toClient;
  queue.push_back(Packet{_delivery, _stream, std::vector<std::uint8_t>(_bytes.begin(), _bytes.end())});
  return true;
}

std::optional<Packet> LoopbackTransport::Receive(Peer _to)
{
  std::deque<Packet>& queue = _to == Peer::Client ? m_toClient : m_toServer;
  if (queue.empty())
  {
    return std::nullopt;
  }
  Packet packet = std::move(queue.front());
  queue.pop_front();
  return packet;
}

std::size_t LoopbackTransport::Pending(Peer _to) const noexcept
{
  return _to == Peer::Client ? m_toClient.size() : m_toServer.size();
}

} // namespace Neuron
