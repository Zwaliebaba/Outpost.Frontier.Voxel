#pragma once

#include "WindowsSdk.h"

#include <msquic.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace Outpost
{

enum class Endpoint : std::uint8_t
{
  Server,
  Client
};

/// What a MsQuic callback saw, copied out of its arguments. MsQuic calls back on its own worker
/// threads; a callback builds one of these, enqueues it and returns, and everything the event
/// means happens on the thread that drains the queue (ADR-017; GameDesign.md §8.1).
struct QuicEvent
{
  enum class Kind : std::uint8_t
  {
    Accepted,            // server: a client connection, already given its handler and configuration
    Connected,           // the handshake completed
    ShutDown,            // the transport or the peer ended the connection; status says why
    ConnectionClosed,    // shutdown is complete and the handle may be closed
    CertificateReceived, // client: the server certificate's fingerprint, awaiting a verdict
    DatagramsReady,      // datagram sending changed; maxSendBytes is what the path carries
    StreamBytes,         // received bytes of the universe stream, in order
    StreamSendDone,      // a stream send finished; its buffer may be freed
    StreamClosed,        // the stream's shutdown is complete and the handle may be closed
    Datagram,            // one received datagram
    DatagramSendState    // a sent datagram changed state; its buffer is freed on a final one
  };

  Endpoint endpoint = Endpoint::Server;
  Kind kind = Kind::Connected;
  HQUIC handle = nullptr;                   // the connection, or for the stream kinds the stream
  QUIC_STATUS status = QUIC_STATUS_SUCCESS; // ShutDown by the transport: why
  std::uint64_t errorCode = 0;              // ShutDown: the application error code on the wire
  bool byPeer = false;                      // ShutDown: the peer ended it, not the transport
  bool sendEnabled = false;
  std::uint16_t maxSendBytes = 0;
  QUIC_DATAGRAM_SEND_STATE sendState = QUIC_DATAGRAM_SEND_UNKNOWN;
  void* sendContext = nullptr;
  std::vector<std::uint8_t> bytes;
};

/// The one place callbacks and the draining thread meet.
class QuicEventQueue
{
public:
  void Push(QuicEvent&& _event);
  [[nodiscard]] std::optional<QuicEvent> Pop();

private:
  std::mutex m_mutex;
  std::deque<QuicEvent> m_events;
};

/// Bytes handed to MsQuic for sending. They must outlive the send, so they are released to MsQuic
/// as the send's context and reclaimed when the event that ends the send is drained.
struct SendBuffer
{
  QUIC_BUFFER buffer{};
  std::vector<std::uint8_t> bytes;
  bool left = false; // a datagram: it has left the send queue (sent, or canceled before sending)
};

} // namespace Outpost
