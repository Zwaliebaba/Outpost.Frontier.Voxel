#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace Neuron
{

/// How a packet travels (ADR-008). Reliable packets all arrive, in order within their stream.
/// Datagrams may be lost or reordered over a network, so whoever receives them keeps the newest.
enum class Delivery : std::uint8_t
{
  Reliable,
  Datagram
};

/// Which half of the game a packet goes to or comes from.
enum class Peer : std::uint8_t
{
  Client,
  Server
};

struct Packet
{
  Delivery delivery = Delivery::Reliable;
  std::uint16_t stream = 0; // reliable packets: order is kept per stream; datagrams: unused
  std::vector<std::uint8_t> bytes;
};

/// The in-process transport: both halves in one process, on one thread. Every packet arrives,
/// in the order sent, datagrams included. It refuses a datagram larger than a network would
/// carry, so a message that would not fit one over QUIC fails here too, from the first slice.
class LoopbackTransport
{
public:
  // The datagram payload QUIC can be relied on to carry on any path, with room left for QUIC's own
  // packet header, packet number, authentication tag and frame header (ADR-008).
  static constexpr std::size_t MAX_DATAGRAM_BYTES = 1'100;

  /// Queues a packet for the other peer. False, and nothing queued, for an oversized datagram.
  [[nodiscard]] bool Send(Peer _from, Delivery _delivery, std::uint16_t _stream, std::span<const std::uint8_t> _bytes);

  /// The next packet for a peer, or nothing.
  [[nodiscard]] std::optional<Packet> Receive(Peer _to);

  [[nodiscard]] std::size_t Pending(Peer _to) const noexcept;

private:
  std::deque<Packet> m_toClient;
  std::deque<Packet> m_toServer;
};

} // namespace Neuron
