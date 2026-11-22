#include "QuicPeer.h"
#include "SelfSignedCertificate.h"

#include <memory>
#include <utility>

namespace Outpost
{

namespace
{

constexpr std::uint64_t IDLE_TIMEOUT_MILLISECONDS = 5'000;

} // namespace

QuicPeer::QuicPeer(const QUIC_API_TABLE& _api, HQUIC _registration, QuicEventQueue& _queue, Endpoint _endpoint) noexcept
  : m_api(_api),
    m_registration(_registration),
    m_queue(_queue),
    m_endpoint(_endpoint)
{
  for (std::size_t i = 0; i < m_alpn.size(); ++i)
  {
    m_alpn[i] = static_cast<std::uint8_t>(ALPN[i]);
  }
}

QUIC_BUFFER QuicPeer::Alpn() noexcept
{
  // The API's field is not const, so it points at this copy rather than at the literal.
  return QUIC_BUFFER{static_cast<std::uint32_t>(m_alpn.size()), m_alpn.data()};
}

QuicPeer::~QuicPeer()
{
  if (m_stream != nullptr)
  {
    m_api.StreamClose(m_stream);
  }
  if (m_connection != nullptr)
  {
    m_api.ConnectionClose(m_connection);
  }
  if (m_listener != nullptr)
  {
    m_api.ListenerClose(m_listener);
  }
  if (m_configuration != nullptr)
  {
    m_api.ConfigurationClose(m_configuration);
  }
}

bool QuicPeer::OpenConfiguration(const QUIC_CREDENTIAL_CONFIG& _credential, std::string& _outError)
{
  QUIC_SETTINGS settings{};
  settings.IdleTimeoutMs = IDLE_TIMEOUT_MILLISECONDS;
  settings.IsSet.IdleTimeoutMs = TRUE;
  settings.DatagramReceiveEnabled = TRUE;
  settings.IsSet.DatagramReceiveEnabled = TRUE;
  if (m_endpoint == Endpoint::Client)
  {
    settings.PeerUnidiStreamCount = 1; // the world stream, which the server opens
    settings.IsSet.PeerUnidiStreamCount = TRUE;
  }
  const QUIC_BUFFER alpn = Alpn();
  QUIC_STATUS status = m_api.ConfigurationOpen(m_registration, &alpn, 1, &settings, sizeof(settings), nullptr, &m_configuration);
  if (QUIC_FAILED(status))
  {
    _outError = "ConfigurationOpen failed with " + std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  status = m_api.ConfigurationLoadCredential(m_configuration, &_credential);
  if (QUIC_FAILED(status))
  {
    _outError = "ConfigurationLoadCredential failed with " + std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  return true;
}

bool QuicPeer::Listen(std::uint16_t _port, PCCERT_CONTEXT _certificate, std::string& _outError)
{
  QUIC_CREDENTIAL_CONFIG credential{};
  credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
  credential.Flags = QUIC_CREDENTIAL_FLAG_NONE;
  // The API's field is not const; Schannel only reads the certificate.
  credential.CertificateContext = const_cast<CERT_CONTEXT*>(_certificate);
  if (!OpenConfiguration(credential, _outError))
  {
    return false;
  }
  QUIC_STATUS status = m_api.ListenerOpen(m_registration, &QuicPeer::ListenerCallback, this, &m_listener);
  if (QUIC_FAILED(status))
  {
    _outError = "ListenerOpen failed with " + std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  QUIC_ADDR address{};
  if (!QuicAddrFromString("127.0.0.1", _port, &address))
  {
    _outError = "cannot form the listening address";
    return false;
  }
  const QUIC_BUFFER alpn = Alpn();
  status = m_api.ListenerStart(m_listener, &alpn, 1, &address);
  if (QUIC_FAILED(status))
  {
    _outError = "ListenerStart on port " + std::to_string(_port) + " failed with " + std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  return true;
}

bool QuicPeer::Connect(std::uint16_t _port, std::string& _outError)
{
  QUIC_CREDENTIAL_CONFIG credential{};
  credential.Type = QUIC_CREDENTIAL_TYPE_NONE;
  // Not validated against a root: the client pins what the host presents, so it is told of the
  // certificate and decides itself.
  credential.Flags =
    QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION | QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED;
  if (!OpenConfiguration(credential, _outError))
  {
    return false;
  }
  QUIC_STATUS status = m_api.ConnectionOpen(m_registration, &QuicPeer::ConnectionCallback, this, &m_connection);
  if (QUIC_FAILED(status))
  {
    _outError = "ConnectionOpen failed with " + std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  status = m_api.ConnectionStart(m_connection, m_configuration, QUIC_ADDRESS_FAMILY_INET, "127.0.0.1", _port);
  if (QUIC_FAILED(status))
  {
    _outError = "ConnectionStart failed with " + std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  return true;
}

void QuicPeer::Push(QuicEvent::Kind _kind, HQUIC _handle)
{
  QuicEvent event;
  event.endpoint = m_endpoint;
  event.kind = _kind;
  event.handle = _handle;
  m_queue.Push(std::move(event));
}

// ── Callbacks: MsQuic's worker threads. Copy, enqueue, return. ───────────────────────────────

QUIC_STATUS QUIC_API QuicPeer::ListenerCallback(HQUIC, void* _context, QUIC_LISTENER_EVENT* _event)
{
  auto& peer = *static_cast<QuicPeer*>(_context);
  if (_event->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION)
  {
    return QUIC_STATUS_SUCCESS;
  }
  // The API requires the handler before this callback returns. The configuration is fixed before
  // the listener starts, so setting it here reads nothing the draining thread writes.
  const HQUIC connection = _event->NEW_CONNECTION.Connection;
  peer.m_api.SetCallbackHandler(connection, reinterpret_cast<void*>(&QuicPeer::ConnectionCallback), &peer);
  const QUIC_STATUS status = peer.m_api.ConnectionSetConfiguration(connection, peer.m_configuration);
  if (QUIC_FAILED(status))
  {
    return status;
  }
  peer.Push(QuicEvent::Kind::Accepted, connection);
  return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API QuicPeer::ConnectionCallback(HQUIC _connection, void* _context, QUIC_CONNECTION_EVENT* _event)
{
  auto& peer = *static_cast<QuicPeer*>(_context);
  QuicEvent event;
  event.endpoint = peer.m_endpoint;
  event.handle = _connection;
  switch (_event->Type)
  {
  case QUIC_CONNECTION_EVENT_CONNECTED:
    event.kind = QuicEvent::Kind::Connected;
    break;
  case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
    event.kind = QuicEvent::Kind::ShutDown;
    event.status = _event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status;
    event.errorCode = _event->SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode;
    break;
  case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
    event.kind = QuicEvent::Kind::ShutDown;
    event.byPeer = true;
    event.errorCode = _event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode;
    break;
  case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
    event.kind = QuicEvent::Kind::ConnectionClosed;
    break;
  case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
    // The API requires the handler before this callback returns; nothing else happens here.
    peer.m_api.SetCallbackHandler(_event->PEER_STREAM_STARTED.Stream, reinterpret_cast<void*>(&QuicPeer::StreamCallback), &peer);
    return QUIC_STATUS_SUCCESS;
  case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
    event.kind = QuicEvent::Kind::DatagramsReady;
    event.sendEnabled = _event->DATAGRAM_STATE_CHANGED.SendEnabled != FALSE;
    event.maxSendBytes = _event->DATAGRAM_STATE_CHANGED.MaxSendLength;
    break;
  case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
  {
    const QUIC_BUFFER& buffer = *_event->DATAGRAM_RECEIVED.Buffer;
    event.kind = QuicEvent::Kind::Datagram;
    event.bytes.assign(buffer.Buffer, buffer.Buffer + buffer.Length);
    break;
  }
  case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
    event.kind = QuicEvent::Kind::DatagramSendState;
    event.sendState = _event->DATAGRAM_SEND_STATE_CHANGED.State;
    event.sendContext = _event->DATAGRAM_SEND_STATE_CHANGED.ClientContext;
    break;
  case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
    // The certificate is valid only during this call, so its fingerprint is copied out, and the
    // verdict comes later from the draining thread (CompleteCertificate).
    event.kind = QuicEvent::Kind::CertificateReceived;
    event.bytes = SelfSignedCertificate::Fingerprint(static_cast<PCCERT_CONTEXT>(_event->PEER_CERTIFICATE_RECEIVED.Certificate));
    peer.m_queue.Push(std::move(event));
    return QUIC_STATUS_PENDING;
  default:
    return QUIC_STATUS_SUCCESS;
  }
  peer.m_queue.Push(std::move(event));
  return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API QuicPeer::StreamCallback(HQUIC _stream, void* _context, QUIC_STREAM_EVENT* _event)
{
  auto& peer = *static_cast<QuicPeer*>(_context);
  QuicEvent event;
  event.endpoint = peer.m_endpoint;
  event.handle = _stream;
  switch (_event->Type)
  {
  case QUIC_STREAM_EVENT_RECEIVE:
    event.kind = QuicEvent::Kind::StreamBytes;
    for (std::uint32_t i = 0; i < _event->RECEIVE.BufferCount; ++i)
    {
      const QUIC_BUFFER& buffer = _event->RECEIVE.Buffers[i];
      event.bytes.insert(event.bytes.end(), buffer.Buffer, buffer.Buffer + buffer.Length);
    }
    break;
  case QUIC_STREAM_EVENT_SEND_COMPLETE:
    event.kind = QuicEvent::Kind::StreamSendDone;
    event.sendContext = _event->SEND_COMPLETE.ClientContext;
    break;
  case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
    event.kind = QuicEvent::Kind::StreamClosed;
    break;
  default:
    return QUIC_STATUS_SUCCESS;
  }
  peer.m_queue.Push(std::move(event));
  return QUIC_STATUS_SUCCESS;
}

// ── The draining thread ──────────────────────────────────────────────────────────────────────

void QuicPeer::Handle(const QuicEvent& _event)
{
  switch (_event.kind)
  {
  case QuicEvent::Kind::Accepted:
    m_connection = _event.handle;
    break;
  case QuicEvent::Kind::Connected:
    m_connected = true;
    if (m_endpoint == Endpoint::Server)
    {
      HQUIC stream = nullptr;
      if (QUIC_SUCCEEDED(m_api.StreamOpen(m_connection, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, &QuicPeer::StreamCallback, this, &stream)))
      {
        if (QUIC_SUCCEEDED(m_api.StreamStart(stream, QUIC_STREAM_START_FLAG_IMMEDIATE)))
        {
          m_stream = stream;
        }
        else
        {
          m_api.StreamClose(stream);
        }
      }
    }
    break;
  case QuicEvent::Kind::ShutDown:
    if (!_event.byPeer)
    {
      m_shutdownStatus = _event.status;
    }
    break;
  case QuicEvent::Kind::ConnectionClosed:
    if (m_stream != nullptr)
    {
      m_api.StreamClose(m_stream);
      m_stream = nullptr;
    }
    m_api.ConnectionClose(_event.handle);
    m_connection = nullptr;
    m_connected = false;
    m_closed = true;
    break;
  case QuicEvent::Kind::DatagramsReady:
    m_datagramsEnabled = _event.sendEnabled;
    m_maxDatagramBytes = _event.maxSendBytes;
    break;
  case QuicEvent::Kind::StreamSendDone:
    delete static_cast<SendBuffer*>(_event.sendContext);
    break;
  case QuicEvent::Kind::StreamClosed:
    if (_event.handle == m_stream)
    {
      m_stream = nullptr;
    }
    if (!m_closed)
    {
      m_api.StreamClose(_event.handle);
    }
    break;
  case QuicEvent::Kind::DatagramSendState:
  {
    ++m_datagramStates[static_cast<std::size_t>(_event.sendState)];
    auto* send = static_cast<SendBuffer*>(_event.sendContext);
    if (!send->left)
    {
      send->left = true; // its first state change: sent, or canceled unsent
      --m_queuedDatagrams;
    }
    if (QUIC_DATAGRAM_SEND_STATE_IS_FINAL(_event.sendState))
    {
      delete send;
    }
    break;
  }
  case QuicEvent::Kind::CertificateReceived:
  case QuicEvent::Kind::StreamBytes:
  case QuicEvent::Kind::Datagram:
    break;
  }
}

void QuicPeer::CompleteCertificate(bool _accept)
{
  if (m_connection != nullptr)
  {
    m_api.ConnectionCertificateValidationComplete(m_connection, _accept ? TRUE : FALSE,
                                                  _accept ? QUIC_TLS_ALERT_CODE_SUCCESS : QUIC_TLS_ALERT_CODE_BAD_CERTIFICATE);
  }
}

bool QuicPeer::SendReliable(std::vector<std::uint8_t>&& _bytes)
{
  if (m_stream == nullptr || _bytes.empty())
  {
    return false;
  }
  auto send = std::make_unique<SendBuffer>();
  send->bytes = std::move(_bytes);
  send->buffer = QUIC_BUFFER{static_cast<std::uint32_t>(send->bytes.size()), send->bytes.data()};
  SendBuffer* const context = send.release(); // MsQuic holds it from here; StreamSendDone reclaims it
  if (QUIC_FAILED(m_api.StreamSend(m_stream, &context->buffer, 1, QUIC_SEND_FLAG_NONE, context)))
  {
    delete context;
    return false;
  }
  return true;
}

bool QuicPeer::SendDatagram(std::vector<std::uint8_t>&& _bytes)
{
  if (m_connection == nullptr || !m_datagramsEnabled || _bytes.empty() || _bytes.size() > m_maxDatagramBytes)
  {
    return false;
  }
  auto send = std::make_unique<SendBuffer>();
  send->bytes = std::move(_bytes);
  send->buffer = QUIC_BUFFER{static_cast<std::uint32_t>(send->bytes.size()), send->bytes.data()};
  SendBuffer* const context = send.release(); // MsQuic holds it from here; the final DatagramSendState reclaims it
  if (QUIC_FAILED(m_api.DatagramSend(m_connection, &context->buffer, 1, QUIC_SEND_FLAG_NONE, context)))
  {
    delete context;
    return false;
  }
  ++m_queuedDatagrams;
  return true;
}

void QuicPeer::Shutdown()
{
  if (m_connection != nullptr)
  {
    m_api.ConnectionShutdown(m_connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
  }
}

} // namespace Outpost
