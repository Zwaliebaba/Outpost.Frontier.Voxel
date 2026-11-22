#pragma once

#include "QuicEvents.h"

#include <wincrypt.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Outpost
{

/// One end of one QUIC connection on localhost: the server, which listens and opens the world
/// stream, or the client, which connects and pins the server's certificate. Only the thread that
/// drains the event queue calls its methods; its callbacks only copy and enqueue (ADR-017).
class QuicPeer
{
public:
  static constexpr char ALPN[] = "outpost-frontier";

  QuicPeer(const QUIC_API_TABLE& _api, HQUIC _registration, QuicEventQueue& _queue, Endpoint _endpoint) noexcept;
  ~QuicPeer();
  QuicPeer(const QuicPeer&) = delete;
  QuicPeer& operator=(const QuicPeer&) = delete;

  /// Server: listens on 127.0.0.1 with this certificate.
  [[nodiscard]] bool Listen(std::uint16_t _port, PCCERT_CONTEXT _certificate, std::string& _outError);

  /// Client: connects to 127.0.0.1. The server's certificate arrives as a CertificateReceived
  /// event, and the handshake waits for CompleteCertificate.
  [[nodiscard]] bool Connect(std::uint16_t _port, std::string& _outError);

  /// The bookkeeping every event needs: handles, the world stream, buffers, counters.
  void Handle(const QuicEvent& _event);

  void CompleteCertificate(bool _accept);
  [[nodiscard]] bool SendReliable(std::vector<std::uint8_t>&& _bytes);
  [[nodiscard]] bool SendDatagram(std::vector<std::uint8_t>&& _bytes);
  void Shutdown();

  [[nodiscard]] bool Connected() const noexcept
  {
    return m_connected;
  }
  [[nodiscard]] bool StreamReady() const noexcept
  {
    return m_stream != nullptr;
  }
  [[nodiscard]] bool DatagramsEnabled() const noexcept
  {
    return m_datagramsEnabled;
  }
  [[nodiscard]] std::uint16_t MaxDatagramBytes() const noexcept
  {
    return m_maxDatagramBytes;
  }
  [[nodiscard]] bool Closed() const noexcept
  {
    return m_closed;
  }
  [[nodiscard]] QUIC_STATUS ShutdownStatus() const noexcept
  {
    return m_shutdownStatus;
  }
  /// Datagrams handed to MsQuic that have not left its send queue: the connection could not send
  /// them yet. The server builds state only when this has drained (GameDesign.md §8.1).
  [[nodiscard]] std::uint64_t QueuedDatagrams() const noexcept
  {
    return m_queuedDatagrams;
  }
  [[nodiscard]] std::uint64_t DatagramStates(QUIC_DATAGRAM_SEND_STATE _state) const noexcept
  {
    return m_datagramStates[static_cast<std::size_t>(_state)];
  }

private:
  static QUIC_STATUS QUIC_API ListenerCallback(HQUIC _listener, void* _context, QUIC_LISTENER_EVENT* _event);
  static QUIC_STATUS QUIC_API ConnectionCallback(HQUIC _connection, void* _context, QUIC_CONNECTION_EVENT* _event);
  static QUIC_STATUS QUIC_API StreamCallback(HQUIC _stream, void* _context, QUIC_STREAM_EVENT* _event);

  [[nodiscard]] bool OpenConfiguration(const QUIC_CREDENTIAL_CONFIG& _credential, std::string& _outError);
  [[nodiscard]] QUIC_BUFFER Alpn() noexcept;
  void Push(QuicEvent::Kind _kind, HQUIC _handle);

  const QUIC_API_TABLE& m_api;
  HQUIC m_registration = nullptr;
  QuicEventQueue& m_queue;
  Endpoint m_endpoint = Endpoint::Server;
  std::array<std::uint8_t, sizeof(ALPN) - 1> m_alpn{}; // ALPN without its terminator, as MsQuic takes it

  HQUIC m_configuration = nullptr;
  HQUIC m_listener = nullptr;
  HQUIC m_connection = nullptr;
  HQUIC m_stream = nullptr;
  bool m_connected = false;
  bool m_closed = false;
  bool m_datagramsEnabled = false;
  std::uint16_t m_maxDatagramBytes = 0;
  std::uint64_t m_queuedDatagrams = 0;
  QUIC_STATUS m_shutdownStatus = QUIC_STATUS_SUCCESS;
  std::array<std::uint64_t, 8> m_datagramStates{};
};

} // namespace Outpost
