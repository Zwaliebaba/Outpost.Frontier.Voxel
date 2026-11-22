#include "LoopbackTransport.h"
#include "Messages.h"
#include "WireFormat.h"

#include <cstdint>
#include <optional>
#include <variant>
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

[[nodiscard]] Outpost::WorldObject SampleObject()
{
  Outpost::WorldObject object;
  object.id = 12;
  object.name = "Frigate";
  object.model = "Frigate";
  object.owner = Outpost::Owner::Hostile;
  object.scale = 1.25f;
  object.pose.position = Outpost::WorldPosition{1.0e9 + 0.25, -3.5, 42.0};
  object.pose.orientation = DirectX::XMFLOAT4{0.0f, 0.70710677f, 0.0f, 0.70710677f};
  object.poseTick = 9;
  return object;
}

[[nodiscard]] std::vector<Outpost::Message> OneOfEach()
{
  return {Outpost::TickMessage{123'456'789'012ull},
          SampleShip(),
          Outpost::ObjectiveMessage{2, {"Escort tug T-041 to dock", true}},
          Outpost::ResourceMessage{77, 1, {"Ore", -5, 60}},
          Outpost::ObjectSpawnMessage{SampleObject()},
          Outpost::ObjectStateMessage{10, 12, SampleObject().pose}};
}

/// The decoded message; fails the test if the bytes do not decode.
[[nodiscard]] Outpost::Message Decoded(std::span<const std::uint8_t> _bytes)
{
  auto decoded = Outpost::Decode(_bytes);
  if (!decoded.has_value())
  {
    Assert::Fail(L"the bytes did not decode"); // does not return
  }
  return std::move(*decoded);
}

void AssertError(const std::vector<std::uint8_t>& _bytes, Outpost::DecodeError _expected, const wchar_t* _why)
{
  const auto decoded = Outpost::Decode(_bytes);
  if (decoded.has_value())
  {
    Assert::Fail(_why); // does not return
  }
  Assert::IsTrue(decoded.error() == _expected, _why);
}

/// The next packet for a peer; fails the test if there is none.
[[nodiscard]] Neuron::Packet Next(Neuron::LoopbackTransport& _transport, Neuron::Peer _to)
{
  std::optional<Neuron::Packet> packet = _transport.Receive(_to);
  if (!packet.has_value())
  {
    Assert::Fail(L"no packet was waiting"); // does not return
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
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::ObjectSpawn) ==
                   Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_WORLD});
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::ObjectState) ==
                   Outpost::Route{Neuron::Delivery::Datagram, Outpost::STREAM_WORLD});
  }

  TEST_METHOD(APositionKeepsDoublePrecision)
  {
    // A quarter metre a billion metres out survives the wire, which a float would not carry.
    const Outpost::Message decoded = Decoded(Outpost::Encode(Outpost::ObjectSpawnMessage{SampleObject()}));
    Assert::AreEqual(1.0e9 + 0.25, std::get<Outpost::ObjectSpawnMessage>(decoded).object.pose.position.x);
  }

  TEST_METHOD(AnUnknownOwnerIsRefused)
  {
    std::vector<std::uint8_t> bytes = Outpost::Encode(Outpost::ObjectSpawnMessage{SampleObject()});
    // The owner byte follows the id and the two names, each a 16-bit length and 7 bytes.
    const std::size_t ownerOffset = Outpost::MESSAGE_HEADER_BYTES + 4 + (2 + 7) + (2 + 7);
    Assert::AreEqual(std::uint8_t{2}, bytes[ownerOffset], L"the offset lands on the owner");
    bytes[ownerOffset] = 3;
    AssertError(bytes, Outpost::DecodeError::Malformed, L"owner 3 is not in the catalogue");
  }

  TEST_METHOD(EveryDatagramMessageFitsTheDatagramBudget)
  {
    // The largest a datagram message can be: names at a generous 255 bytes.
    const std::string longName(255, 'n');
    const std::vector<Outpost::Message> largest{Outpost::TickMessage{~0ull}, Outpost::ResourceMessage{~0ull, 0xFFFF, {longName, -1, -1}},
                                                Outpost::ObjectStateMessage{~0ull, ~0u, SampleObject().pose}};
    for (const Outpost::Message& message : largest)
    {
      Assert::IsTrue(Outpost::RouteOf(Outpost::KindOf(message)).delivery == Neuron::Delivery::Datagram);
      Assert::IsTrue(Outpost::Encode(message).size() <= Neuron::LoopbackTransport::MAX_DATAGRAM_BYTES);
    }
  }

  TEST_METHOD(ASpawnAddsAnObjectAndRaisesTheRevision)
  {
    Outpost::WorldSnapshot world;
    Outpost::ApplyMessage(Outpost::ObjectSpawnMessage{SampleObject()}, world);
    Assert::AreEqual(std::size_t{1}, world.objects.size());
    Assert::IsTrue(world.objects[0] == SampleObject());
    Assert::AreEqual(std::uint64_t{1}, world.objectsRevision);
  }

  TEST_METHOD(OnlyANewerPoseMovesAnObject)
  {
    Outpost::WorldSnapshot world;
    Outpost::ApplyMessage(Outpost::ObjectSpawnMessage{SampleObject()}, world); // pose from tick 9
    Outpost::Pose moved = SampleObject().pose;
    moved.position.y += 10.0;

    Outpost::ApplyMessage(Outpost::ObjectStateMessage{8, 12, moved}, world);
    Assert::IsTrue(world.objects[0].pose == SampleObject().pose, L"an older pose is dropped");

    Outpost::ApplyMessage(Outpost::ObjectStateMessage{11, 12, moved}, world);
    Assert::IsTrue(world.objects[0].pose == moved);
    Assert::AreEqual(std::uint64_t{11}, world.objects[0].poseTick);
    Assert::AreEqual(std::uint64_t{2}, world.objectsRevision);

    Outpost::ApplyMessage(Outpost::ObjectStateMessage{12, 12, moved}, world);
    Assert::AreEqual(std::uint64_t{12}, world.objects[0].poseTick, L"the same pose, newer, still advances the tick");
    Assert::AreEqual(std::uint64_t{2}, world.objectsRevision, L"... without a rebuild");
  }

  TEST_METHOD(StateForAnUnknownObjectChangesNothing)
  {
    Outpost::WorldSnapshot world;
    Outpost::ApplyMessage(Outpost::ObjectStateMessage{5, 99, SampleObject().pose}, world);
    Assert::IsTrue(world.objects.empty());
    Assert::AreEqual(std::uint64_t{0}, world.objectsRevision);
  }

  TEST_METHOD(AnOlderResourceIsDropped)
  {
    Outpost::WorldSnapshot world;
    Outpost::ApplyMessage(Outpost::ResourceMessage{20, 1, {"Ore", 50, 60}}, world);
    Outpost::ApplyMessage(Outpost::ResourceMessage{19, 1, {"Ore", 40, 60}}, world);
    Assert::AreEqual(std::size_t{2}, world.resources.size(), L"index 1 makes room for index 0");
    Assert::AreEqual(std::int64_t{50}, world.resources[1].amount);
  }

  TEST_METHOD(AShipIsKeyedByItsObject)
  {
    Outpost::WorldSnapshot world;
    Outpost::ShipMessage ship = SampleShip();
    ship.ship.object = 4;
    Outpost::ApplyMessage(ship, world);
    ship.ship.hullPercent = 12;
    Outpost::ApplyMessage(ship, world);
    Assert::AreEqual(std::size_t{1}, world.ships.size());
    Assert::AreEqual(12, world.ships[0].hullPercent);
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
