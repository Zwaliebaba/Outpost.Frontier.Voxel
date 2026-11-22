// QuicSpike: the message catalogue over MsQuic on localhost (plan task F7, ADR-017).
//
// One process hosts both ends. The server sends what the game's server sends: every kind in the
// catalogue, encoded by GameCore, each in the delivery class RouteOf gives it: facts on one
// reliable stream, state as datagrams whose newest copy wins. Three connections are made:
//
//   1. directly, with a fresh host certificate, which the client pins on this first connection;
//   2. through a relay that drops a share of the packets both ways, with the same certificate;
//   3. directly, with a new certificate: the client must refuse it.
//
// It reports what crossed and checks it, and exits 0 only if every check holds. It is a tool:
// nothing in the game or its test suites runs it, and only it loads msquic.dll.

#include "WindowsSdk.h"

#include <winsock2.h>

#include "LossyRelay.h"
#include "QuicEvents.h"
#include "QuicLibrary.h"
#include "QuicPeer.h"
#include "SelfSignedCertificate.h"

#include "LoopbackTransport.h"
#include "Messages.h"
#include "WorldSnapshot.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Outpost
{

namespace
{

constexpr std::uint16_t SERVER_PORT = 47'100;
constexpr std::uint16_t RELAY_PORT = 47'101;
constexpr int TICKS_PER_SECOND = 60;
constexpr int DEFAULT_TICKS = 600;
constexpr int DEFAULT_OBJECTS = 50;
constexpr int DEFAULT_LOSS_PERCENT = 10;
constexpr int FACT_INTERVAL_TICKS = 30;
constexpr std::uint64_t RELAY_SEED = 0x0F7'5EED;
// "Current": no object's pose, as the client holds it, is ever more than this behind the newest
// tick the server has sent. Half a second; set before the first measurement.
constexpr std::uint64_t MAX_STALE_TICKS = 30;
constexpr auto CONNECT_TIMEOUT = std::chrono::seconds(10);
constexpr auto SETTLE_TIMEOUT = std::chrono::seconds(10);
constexpr std::size_t KIND_COUNT = 7; // MessageKind values 1 to 6, indexed directly

struct Options
{
  int ticks = DEFAULT_TICKS;
  int objects = DEFAULT_OBJECTS;
  int lossPercent = DEFAULT_LOSS_PERCENT;
  bool gate = true; // build a tick's state only once the last tick's datagrams have left the queue
};

/// Host identity as the client remembers it: empty until the first connection pins it.
struct PinStore
{
  std::optional<std::vector<std::uint8_t>> fingerprint;
};

struct ServerTraffic
{
  std::vector<std::vector<std::uint8_t>> reliable; // every reliable message, in send order
  std::array<std::uint64_t, KIND_COUNT> sentByKind{};
  std::uint64_t datagramsSent = 0;
  std::uint64_t datagramsRefused = 0;
  std::uint64_t reliableRefused = 0;
  std::size_t largestDatagramBytes = 0;
  std::uint64_t lastTick = 0;
  std::uint64_t withheldTicks = 0; // ticks whose state was not built because the connection could not send
};

struct ClientTraffic
{
  std::vector<std::uint8_t> streamBuffer;
  std::vector<std::vector<std::uint8_t>> reliable; // the messages the stream delivered, in arrival order
  std::array<std::uint64_t, KIND_COUNT> reliableByKind{};
  std::array<std::uint64_t, KIND_COUNT> datagramByKind{};
  std::uint64_t wrongRoute = 0;
  std::uint64_t undecodable = 0;
  std::uint64_t olderThanHeld = 0;
  std::uint64_t staleSamples = 0;
  std::uint64_t staleSumTicks = 0;
  std::uint64_t staleMaxTicks = 0;
  std::optional<bool> certificateAccepted;
  bool pinnedHere = false;
  WorldSnapshot world;
};

struct RunSpec
{
  const char* name = "";
  PCCERT_CONTEXT certificate = nullptr;
  bool viaRelay = false;
  bool expectRefused = false;
};

constexpr std::string_view KIND_NAMES[KIND_COUNT] = {"?", "Tick", "Ship", "Objective", "Resource", "ObjectSpawn", "ObjectState"};

std::size_t KindIndex(MessageKind _kind)
{
  return std::min(static_cast<std::size_t>(_kind), KIND_COUNT - 1);
}

int g_failures = 0;

void Check(bool _holds, const std::string& _what)
{
  std::printf("  %s %s\n", _holds ? "PASS" : "FAIL", _what.c_str());
  if (!_holds)
  {
    ++g_failures;
  }
}

// ── What the server sends ────────────────────────────────────────────────────────────────────

Pose PoseAt(std::uint64_t _tick, int _object)
{
  const double radiusMetres = 100.0 + 10.0 * _object;
  const double angleRadians = 0.01 * static_cast<double>(_tick) + 0.7 * _object;
  Pose pose;
  pose.position = WorldPosition{radiusMetres * std::cos(angleRadians), 5.0 * _object, radiusMetres * std::sin(angleRadians)};
  const float half = static_cast<float>(angleRadians * 0.5);
  pose.orientation = DirectX::XMFLOAT4{0.0f, std::sin(half), 0.0f, std::cos(half)};
  return pose;
}

ObjectId IdOf(int _object)
{
  return static_cast<ObjectId>(_object + 1);
}

void Send(QuicPeer& _server, const Message& _message, ServerTraffic& _traffic)
{
  std::vector<std::uint8_t> bytes = Encode(_message);
  const MessageKind kind = KindOf(_message);
  ++_traffic.sentByKind[KindIndex(kind)];
  if (RouteOf(kind).delivery == Neuron::Delivery::Reliable)
  {
    _traffic.reliable.push_back(bytes);
    if (!_server.SendReliable(std::move(bytes)))
    {
      ++_traffic.reliableRefused;
    }
    return;
  }
  _traffic.largestDatagramBytes = std::max(_traffic.largestDatagramBytes, bytes.size());
  ++_traffic.datagramsSent;
  if (bytes.size() > Neuron::LoopbackTransport::MAX_DATAGRAM_BYTES || !_server.SendDatagram(std::move(bytes)))
  {
    ++_traffic.datagramsRefused;
  }
}

void SendFacts(QuicPeer& _server, std::uint64_t _tick, int _objects, ServerTraffic& _traffic)
{
  if (_tick == 0)
  {
    for (int i = 0; i < _objects; ++i)
    {
      WorldObject object;
      object.id = IdOf(i);
      object.name = "Object" + std::to_string(i);
      object.model = i % 2 == 0 ? "EscortLancer" : "EscortTalon";
      object.owner = i == 0 ? Owner::Player : Owner::Neutral;
      object.pose = PoseAt(0, i);
      Send(_server, ObjectSpawnMessage{object}, _traffic);
    }
  }
  const std::uint64_t round = _tick / FACT_INTERVAL_TICKS;
  ShipState ship;
  ship.instance = "Object0";
  ship.callsign = "Lancer";
  ship.shipClass = "Escort";
  ship.hullPercent = 100 - static_cast<int>(round % 100);
  ship.object = IdOf(0);
  Send(_server, ShipMessage{ship}, _traffic);
  const auto index = static_cast<std::uint16_t>(round % 2);
  Send(_server, ObjectiveMessage{index, ObjectiveState{"Objective " + std::to_string(round), round % 3 == 0}}, _traffic);
}

void SendState(QuicPeer& _server, std::uint64_t _tick, int _objects, ServerTraffic& _traffic)
{
  Send(_server, TickMessage{_tick}, _traffic);
  for (int i = 0; i < _objects; ++i)
  {
    Send(_server, ObjectStateMessage{_tick, IdOf(i), PoseAt(_tick, i)}, _traffic);
  }
  Send(_server, ResourceMessage{_tick, 0, ResourceState{"Credits", 1'000 + static_cast<std::int64_t>(_tick), 0}}, _traffic);
  Send(_server, ResourceMessage{_tick, 1, ResourceState{"Ore", static_cast<std::int64_t>(_tick % 500), 500}}, _traffic);
  _traffic.lastTick = _tick;
}

// ── What the client receives ─────────────────────────────────────────────────────────────────

void Receive(std::span<const std::uint8_t> _bytes, Neuron::Delivery _arrivedBy, ClientTraffic& _traffic)
{
  const auto decoded = Decode(_bytes);
  if (!decoded)
  {
    ++_traffic.undecodable;
    return;
  }
  const MessageKind kind = KindOf(*decoded);
  if (RouteOf(kind).delivery != _arrivedBy)
  {
    ++_traffic.wrongRoute;
  }
  if (_arrivedBy == Neuron::Delivery::Reliable)
  {
    ++_traffic.reliableByKind[KindIndex(kind)];
    _traffic.reliable.emplace_back(_bytes.begin(), _bytes.end());
  }
  else
  {
    ++_traffic.datagramByKind[KindIndex(kind)];
    if (const auto* state = std::get_if<ObjectStateMessage>(&*decoded))
    {
      const WorldObject* held = _traffic.world.FindObject(state->id);
      if (held != nullptr && state->tick < held->poseTick)
      {
        ++_traffic.olderThanHeld;
      }
    }
  }
  ApplyMessage(*decoded, _traffic.world);
}

/// The stream is bytes; each message's header says how long it is.
void ReceiveStreamBytes(const std::vector<std::uint8_t>& _bytes, ClientTraffic& _traffic)
{
  std::vector<std::uint8_t>& buffer = _traffic.streamBuffer;
  buffer.insert(buffer.end(), _bytes.begin(), _bytes.end());
  std::size_t offset = 0;
  while (buffer.size() - offset >= MESSAGE_HEADER_BYTES)
  {
    const std::uint32_t payloadBytes =
      static_cast<std::uint32_t>(buffer[offset + 4]) | (static_cast<std::uint32_t>(buffer[offset + 5]) << 8) |
      (static_cast<std::uint32_t>(buffer[offset + 6]) << 16) | (static_cast<std::uint32_t>(buffer[offset + 7]) << 24);
    const std::size_t messageBytes = MESSAGE_HEADER_BYTES + payloadBytes;
    if (buffer.size() - offset < messageBytes)
    {
      break;
    }
    Receive(std::span<const std::uint8_t>(buffer.data() + offset, messageBytes), Neuron::Delivery::Reliable, _traffic);
    offset += messageBytes;
  }
  buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(offset));
}

void SampleStaleness(std::uint64_t _serverTick, ClientTraffic& _traffic)
{
  for (const WorldObject& object : _traffic.world.objects)
  {
    if (object.poseTick == 0 || object.poseTick > _serverTick)
    {
      continue;
    }
    const std::uint64_t staleTicks = _serverTick - object.poseTick;
    ++_traffic.staleSamples;
    _traffic.staleSumTicks += staleTicks;
    _traffic.staleMaxTicks = std::max(_traffic.staleMaxTicks, staleTicks);
  }
}

// ── One connection ───────────────────────────────────────────────────────────────────────────

struct Session
{
  QuicEventQueue queue;
  QuicPeer server;
  QuicPeer client;
  PinStore& pins;
  ClientTraffic received;

  Session(const QUIC_API_TABLE& _api, HQUIC _registration, PinStore& _pins)
    : server(_api, _registration, queue, Endpoint::Server),
      client(_api, _registration, queue, Endpoint::Client),
      pins(_pins)
  {
  }

  void Drain()
  {
    while (auto event = queue.Pop())
    {
      if (event->endpoint == Endpoint::Client)
      {
        switch (event->kind)
        {
        case QuicEvent::Kind::CertificateReceived:
        {
          bool accept = false;
          if (!pins.fingerprint.has_value())
          {
            pins.fingerprint = event->bytes; // trust on first use
            received.pinnedHere = true;
            accept = !event->bytes.empty();
          }
          else
          {
            accept = !event->bytes.empty() && *pins.fingerprint == event->bytes;
          }
          received.certificateAccepted = accept;
          client.CompleteCertificate(accept);
          break;
        }
        case QuicEvent::Kind::StreamBytes:
          ReceiveStreamBytes(event->bytes, received);
          break;
        case QuicEvent::Kind::Datagram:
          Receive(event->bytes, Neuron::Delivery::Datagram, received);
          break;
        default:
          break;
        }
      }
      (event->endpoint == Endpoint::Server ? server : client).Handle(*event);
    }
  }

  bool WaitFor(const std::function<bool()>& _condition, std::chrono::steady_clock::duration _timeout)
  {
    const auto deadline = std::chrono::steady_clock::now() + _timeout;
    for (;;)
    {
      Drain();
      if (_condition())
      {
        return true;
      }
      if (std::chrono::steady_clock::now() >= deadline)
      {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
};

std::string Hex(std::uint64_t _value)
{
  char text[24]{};
  std::snprintf(text, sizeof(text), "0x%08llX", static_cast<unsigned long long>(_value));
  return text;
}

void ReportCrossings(const ServerTraffic& _sent, const ClientTraffic& _received)
{
  for (std::size_t kind = 1; kind < KIND_COUNT; ++kind)
  {
    const Route route = RouteOf(static_cast<MessageKind>(kind));
    const bool reliable = route.delivery == Neuron::Delivery::Reliable;
    const std::uint64_t arrived = reliable ? _received.reliableByKind[kind] : _received.datagramByKind[kind];
    const std::uint64_t other = reliable ? _received.datagramByKind[kind] : _received.reliableByKind[kind];
    std::printf("  %-12s %-8s sent %7llu, arrived %7llu, by the other class %llu\n", std::string(KIND_NAMES[kind]).c_str(),
                reliable ? "stream" : "datagram", static_cast<unsigned long long>(_sent.sentByKind[kind]),
                static_cast<unsigned long long>(arrived), static_cast<unsigned long long>(other));
  }
}

void RunConnection(const QUIC_API_TABLE& _api, HQUIC _registration, const Options& _options, const RunSpec& _spec, PinStore& _pins)
{
  std::printf("\n%s\n", _spec.name);
  Session session(_api, _registration, _pins);
  ServerTraffic sent;
  std::string error;
  if (!session.server.Listen(SERVER_PORT, _spec.certificate, error))
  {
    Check(false, "the server listens: " + error);
    return;
  }
  LossyRelay relay;
  if (_spec.viaRelay && !relay.Start(RELAY_PORT, SERVER_PORT, _options.lossPercent, RELAY_SEED, error))
  {
    Check(false, "the relay starts: " + error);
    return;
  }
  if (!session.client.Connect(_spec.viaRelay ? RELAY_PORT : SERVER_PORT, error))
  {
    Check(false, "the client connects: " + error);
    return;
  }

  if (_spec.expectRefused)
  {
    session.WaitFor([&] { return session.client.Closed(); }, CONNECT_TIMEOUT);
    session.WaitFor([&] { return session.server.Closed(); }, std::chrono::seconds(2));
    std::printf("  client connection ended with status %s\n", Hex(static_cast<std::uint32_t>(session.client.ShutdownStatus())).c_str());
    Check(session.received.certificateAccepted == false, "the client was shown the changed certificate and refused it");
    Check(!session.client.Connected() && session.client.Closed() && QUIC_FAILED(session.client.ShutdownStatus()),
          "the connection failed instead of completing");
    Check(session.received.reliable.empty() && session.received.datagramByKind == std::array<std::uint64_t, KIND_COUNT>{},
          "no message reached the client");
    return;
  }

  const bool ready = session.WaitFor(
    [&] {
      return session.client.Connected() && session.server.Connected() && session.server.StreamReady() && session.server.DatagramsEnabled();
    },
    CONNECT_TIMEOUT);
  Check(ready, "connected, with the world stream open and datagrams enabled");
  if (!ready)
  {
    session.client.Shutdown();
    session.WaitFor([&] { return session.client.Closed() && session.server.Closed(); }, SETTLE_TIMEOUT);
    return;
  }
  std::printf("  certificate %s; server may send datagrams of up to %u bytes on this path\n",
              session.received.pinnedHere ? "pinned on this first connection" : "matched the pin", session.server.MaxDatagramBytes());

  // Real time at the game's tick rate: the pacing is the tool's, not a game rule.
  const auto start = std::chrono::steady_clock::now();
  for (int tick = 0; tick <= _options.ticks; ++tick)
  {
    std::this_thread::sleep_until(start + std::chrono::microseconds(1'000'000LL * tick / TICKS_PER_SECOND));
    session.Drain();
    const auto serverTick = static_cast<std::uint64_t>(tick);
    if (tick > 0)
    {
      SampleStaleness(serverTick, session.received); // against the server's tick, sent or not
    }
    if (serverTick % FACT_INTERVAL_TICKS == 0)
    {
      SendFacts(session.server, serverTick, _options.objects, sent);
    }
    if (!_options.gate || session.server.QueuedDatagrams() == 0)
    {
      SendState(session.server, serverTick + 1, _options.objects, sent);
    }
    else
    {
      ++sent.withheldTicks; // newest wins, so a state not built is a state nobody misses
    }
  }
  const bool settled = session.WaitFor([&] { return session.received.reliable.size() >= sent.reliable.size(); }, SETTLE_TIMEOUT);

  ReportCrossings(sent, session.received);
  std::printf("  datagrams: %llu sent, %llu refused at send; largest %zu bytes (budget %zu); state withheld on %llu of %d ticks (%s)\n",
              static_cast<unsigned long long>(sent.datagramsSent), static_cast<unsigned long long>(sent.datagramsRefused),
              sent.largestDatagramBytes, Neuron::LoopbackTransport::MAX_DATAGRAM_BYTES, static_cast<unsigned long long>(sent.withheldTicks),
              _options.ticks + 1, _options.gate ? "gated on the send queue" : "not gated");
  std::printf("  datagram send states: acknowledged %llu, lost %llu (suspected %llu, acknowledged after suspicion %llu), canceled %llu\n",
              static_cast<unsigned long long>(session.server.DatagramStates(QUIC_DATAGRAM_SEND_ACKNOWLEDGED)),
              static_cast<unsigned long long>(session.server.DatagramStates(QUIC_DATAGRAM_SEND_LOST_DISCARDED)),
              static_cast<unsigned long long>(session.server.DatagramStates(QUIC_DATAGRAM_SEND_LOST_SUSPECT)),
              static_cast<unsigned long long>(session.server.DatagramStates(QUIC_DATAGRAM_SEND_ACKNOWLEDGED_SPURIOUS)),
              static_cast<unsigned long long>(session.server.DatagramStates(QUIC_DATAGRAM_SEND_CANCELED)));
  const double meanStaleTicks = session.received.staleSamples == 0 ? 0.0
                                                                   : static_cast<double>(session.received.staleSumTicks) /
                                                                       static_cast<double>(session.received.staleSamples);
  std::printf("  pose staleness at the client: mean %.2f ticks, worst %llu ticks, over %llu samples; %llu datagram(s) arrived older than "
              "the copy held\n",
              meanStaleTicks, static_cast<unsigned long long>(session.received.staleMaxTicks),
              static_cast<unsigned long long>(session.received.staleSamples),
              static_cast<unsigned long long>(session.received.olderThanHeld));
  if (_spec.viaRelay)
  {
    relay.Stop();
    std::printf("  relay: %llu packets forwarded, %llu dropped (%d %% asked)\n", static_cast<unsigned long long>(relay.Forwarded()),
                static_cast<unsigned long long>(relay.Dropped()), _options.lossPercent);
  }

  bool everyKindArrived = session.received.wrongRoute == 0;
  for (std::size_t kind = 1; kind < KIND_COUNT; ++kind)
  {
    const bool reliable = RouteOf(static_cast<MessageKind>(kind)).delivery == Neuron::Delivery::Reliable;
    everyKindArrived = everyKindArrived && (reliable ? session.received.reliableByKind[kind] : session.received.datagramByKind[kind]) > 0;
  }
  Check(everyKindArrived, "every message kind crossed, each only in its delivery class");
  Check(session.received.undecodable == 0, "every message decoded");
  Check(settled && sent.reliableRefused == 0 && session.received.reliable == sent.reliable,
        "the stream delivered every reliable message, byte for byte, in the order sent (" + std::to_string(sent.reliable.size()) + ")");
  Check(sent.largestDatagramBytes <= Neuron::LoopbackTransport::MAX_DATAGRAM_BYTES &&
          sent.largestDatagramBytes <= session.server.MaxDatagramBytes(),
        "every state message fits the datagram budget and the path");
  Check(session.received.staleMaxTicks <= MAX_STALE_TICKS,
        "state stayed current: no pose more than " + std::to_string(MAX_STALE_TICKS) + " ticks behind the server");
  if (_spec.viaRelay)
  {
    Check(relay.Dropped() > 0 && session.server.DatagramStates(QUIC_DATAGRAM_SEND_LOST_DISCARDED) > 0, "the relay's loss reached QUIC");
  }

  session.client.Shutdown();
  Check(session.WaitFor([&] { return session.client.Closed() && session.server.Closed(); }, SETTLE_TIMEOUT), "both ends closed cleanly");
}

bool ParseInt(const char* _text, int& _outValue)
{
  const std::string_view text = _text;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), _outValue);
  return error == std::errc{} && end == text.data() + text.size();
}

bool ParseOptions(int _argc, char** _argv, Options& _options)
{
  for (int i = 1; i < _argc; ++i)
  {
    const std::string_view argument = _argv[i];
    const bool hasValue = i + 1 < _argc;
    if (argument == "--ticks" && hasValue)
    {
      if (!ParseInt(_argv[++i], _options.ticks))
      {
        return false;
      }
    }
    else if (argument == "--objects" && hasValue)
    {
      if (!ParseInt(_argv[++i], _options.objects))
      {
        return false;
      }
    }
    else if (argument == "--loss" && hasValue)
    {
      if (!ParseInt(_argv[++i], _options.lossPercent))
      {
        return false;
      }
    }
    else if (argument == "--no-gate")
    {
      _options.gate = false;
    }
    else
    {
      return false;
    }
  }
  return _options.ticks > 0 && _options.objects > 0 && _options.lossPercent >= 0 && _options.lossPercent < 100;
}

} // namespace

int Run(int _argc, char** _argv)
{
  Options options;
  if (!ParseOptions(_argc, _argv, options))
  {
    std::printf("usage: QuicSpike [--ticks N] [--objects N] [--loss PERCENT] [--no-gate]\n"
                "  defaults: %d ticks at %d Hz, %d objects, %d %% loss through the relay; a tick's state is built\n"
                "  only once the connection has sent the last tick's, unless --no-gate\n",
                DEFAULT_TICKS, TICKS_PER_SECOND, DEFAULT_OBJECTS, DEFAULT_LOSS_PERCENT);
    return 2;
  }

  WSADATA winsock{};
  if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
  {
    std::printf("QuicSpike: Winsock did not start\n");
    return 1;
  }
  std::string error;
  int exitCode = 1;
  {
    QuicLibrary library;
    SelfSignedCertificate host;
    SelfSignedCertificate impostor;
    if (!library.Load(error) || !host.Create(L"OutpostQuicSpikeHost", error) || !impostor.Create(L"OutpostQuicSpikeImpostor", error))
    {
      std::printf("QuicSpike: %s\n", error.c_str());
    }
    else
    {
      const QUIC_API_TABLE& api = library.Api();
      const QUIC_REGISTRATION_CONFIG registrationConfig{"OutpostQuicSpike", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
      HQUIC registration = nullptr;
      const QUIC_STATUS status = api.RegistrationOpen(&registrationConfig, &registration);
      if (QUIC_FAILED(status))
      {
        std::printf("QuicSpike: RegistrationOpen failed with %s\n", Hex(static_cast<std::uint32_t>(status)).c_str());
      }
      else
      {
        std::printf("QuicSpike: catalogue version %u, %d ticks at %d Hz, %d objects\n", CATALOGUE_VERSION, options.ticks, TICKS_PER_SECOND,
                    options.objects);
        PinStore pins;
        RunConnection(api, registration, options,
                      RunSpec{"1. First connection, direct: the client pins the host", host.Context(), false, false}, pins);
        const std::string lossy = "2. Through the relay, " + std::to_string(options.lossPercent) + " % loss each way, same host";
        RunConnection(api, registration, options, RunSpec{lossy.c_str(), host.Context(), true, false}, pins);
        RunConnection(api, registration, options, RunSpec{"3. Direct, the host's certificate changed", impostor.Context(), false, true},
                      pins);
        api.RegistrationClose(registration);
        std::printf("\nQuicSpike: %s\n",
                    g_failures == 0 ? "every check passed" : (std::to_string(g_failures) + " check(s) failed").c_str());
        exitCode = g_failures == 0 ? 0 : 1;
      }
    }
  }
  WSACleanup();
  return exitCode;
}

} // namespace Outpost

int main(int _argc, char** _argv)
{
  try
  {
    return Outpost::Run(_argc, _argv);
  }
  catch (const std::exception& error)
  {
    std::printf("QuicSpike: %s\n", error.what());
    return 1;
  }
}
