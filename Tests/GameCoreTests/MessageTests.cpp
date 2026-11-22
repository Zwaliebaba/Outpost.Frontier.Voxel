#include "LoopbackTransport.h"
#include "Messages.h"
#include "WireFormat.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

[[nodiscard]] Outpost::ShipMessage SampleShip()
{
  Outpost::ShipMessage message;
  message.ship.instance = "Frigate";
  message.ship.callsign = "F-317";
  message.ship.shipClass = "Frigate";
  message.ship.hullPercent = 68;
  message.ship.modules = {{"Hull Section (B)", 34}, {"Engineering", 72}, {"Reactor", 100}};
  return message;
}

[[nodiscard]] std::vector<Outpost::Message> OneOfEach()
{
  return {Outpost::TickMessage{123'456'789'012ull}, SampleShip(), Outpost::ObjectiveMessage{2, {"Escort tug T-041 to dock", true}},
          Outpost::ResourceMessage{77, 1, {"Ore", -5, 60}}};
}

/// The decoded message; fails the test if the bytes do not decode.
[[nodiscard]] Outpost::Message Decoded(std::span<const std::uint8_t> _bytes)
{
  auto decoded = Outpost::Decode(_bytes);
  if (!decoded.has_value())
  {
    Assert::Fail(L"the bytes did not decode");
    return {};
  }
  return std::move(*decoded);
}

void AssertError(const std::vector<std::uint8_t>& _bytes, Outpost::DecodeError _expected, const wchar_t* _why)
{
  const auto decoded = Outpost::Decode(_bytes);
  if (decoded.has_value())
  {
    Assert::Fail(_why);
    return;
  }
  Assert::IsTrue(decoded.error() == _expected, _why);
}

/// The next packet for a peer; fails the test if there is none.
[[nodiscard]] Neuron::Packet Next(Neuron::LoopbackTransport& _transport, Neuron::Peer _to)
{
  std::optional<Neuron::Packet> packet = _transport.Receive(_to);
  if (!packet.has_value())
  {
    Assert::Fail(L"no packet was waiting");
    return {};
  }
  return std::move(*packet);
}

} // namespace

TEST_CLASS(MessageTests)
{
public:
  TEST_METHOD(EveryKindRoundTrips)
  {
    for (const Outpost::Message& message : OneOfEach())
    {
      const std::vector<std::uint8_t> bytes = Outpost::Encode(message);
      Assert::IsFalse(bytes.empty());
      Assert::IsTrue(Decoded(bytes) == message);
    }
  }

  TEST_METHOD(TheHeaderCarriesVersionKindAndLength)
  {
    const std::vector<std::uint8_t> bytes = Outpost::Encode(Outpost::TickMessage{1});
    Neuron::WireReader header(bytes);
    Assert::IsTrue(Outpost::CATALOGUE_VERSION == header.Uint16());
    Assert::IsTrue(static_cast<std::uint16_t>(Outpost::MessageKind::Tick) == header.Uint16());
    Assert::AreEqual(std::uint32_t{8}, header.Uint32(), L"a tick's payload is one 64-bit value");
    Assert::AreEqual(Outpost::MESSAGE_HEADER_BYTES + 8, bytes.size());
  }

  TEST_METHOD(AnotherCatalogueIsRefused)
  {
    std::vector<std::uint8_t> bytes = Outpost::Encode(SampleShip());
    bytes[0] = static_cast<std::uint8_t>(Outpost::CATALOGUE_VERSION + 1);
    AssertError(bytes, Outpost::DecodeError::WrongVersion, L"version");
  }

  TEST_METHOD(AnUnknownKindIsRefused)
  {
    std::vector<std::uint8_t> bytes = Outpost::Encode(Outpost::TickMessage{1});
    bytes[2] = 0xFF;
    bytes[3] = 0x7F;
    AssertError(bytes, Outpost::DecodeError::UnknownKind, L"kind");
  }

  TEST_METHOD(TruncatedOrPaddedBytesAreRefused)
  {
    const std::vector<std::uint8_t> bytes = Outpost::Encode(SampleShip());
    AssertError({bytes.begin(), bytes.begin() + 5}, Outpost::DecodeError::Truncated, L"shorter than a header");
    AssertError({bytes.begin(), bytes.end() - 1}, Outpost::DecodeError::Truncated, L"shorter than its payload");
    std::vector<std::uint8_t> padded = bytes;
    padded.push_back(0);
    AssertError(padded, Outpost::DecodeError::LengthMismatch, L"longer than its payload");
  }

  TEST_METHOD(APayloadThatDoesNotReadAsItsKindIsRefused)
  {
    std::vector<std::uint8_t> bytes = Outpost::Encode(Outpost::ObjectiveMessage{0, {"Defend the shipyard", true}});
    bytes.back() = 2; // the completion flag is a byte that must be 0 or 1
    AssertError(bytes, Outpost::DecodeError::Malformed, L"flag");
  }

  TEST_METHOD(EachKindHasItsRoute)
  {
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::Tick).delivery == Neuron::Delivery::Datagram);
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::Resource).delivery == Neuron::Delivery::Datagram);
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::Ship) == Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_WORLD});
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::Objective) == Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_WORLD});
  }

  TEST_METHOD(EveryDatagramMessageFitsTheDatagramBudget)
  {
    // The largest a datagram message can be: names at a generous 255 bytes.
    const std::string longName(255, 'n');
    const std::vector<Outpost::Message> largest{Outpost::TickMessage{~0ull}, Outpost::ResourceMessage{~0ull, 0xFFFF, {longName, -1, -1}}};
    for (const Outpost::Message& message : largest)
    {
      Assert::IsTrue(Outpost::RouteOf(Outpost::KindOf(message)).delivery == Neuron::Delivery::Datagram);
      Assert::IsTrue(Outpost::Encode(message).size() <= Neuron::LoopbackTransport::MAX_DATAGRAM_BYTES);
    }
  }

  TEST_METHOD(MessagesCrossTheLoopbackOnTheirRoutes)
  {
    Neuron::LoopbackTransport transport;
    for (const Outpost::Message& message : OneOfEach())
    {
      const Outpost::Route route = Outpost::RouteOf(Outpost::KindOf(message));
      Assert::IsTrue(transport.Send(Neuron::Peer::Server, route.delivery, route.stream, Outpost::Encode(message)));
    }
    for (const Outpost::Message& expected : OneOfEach())
    {
      Assert::IsTrue(Decoded(Next(transport, Neuron::Peer::Client).bytes) == expected);
    }
  }
};

} // namespace GameCoreTests
