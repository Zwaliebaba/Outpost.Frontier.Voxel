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

[[nodiscard]] Outpost::UniverseObject SampleObject()
{
  Outpost::UniverseObject object;
  object.id = 12;
  object.name = "Frigate";
  object.model = "Frigate";
  object.owner = Outpost::Owner::Hostile;
  object.scale = 1.25f;
  object.pose.position = Outpost::UniversePosition{1.0e9 + 0.25, -3.5, 42.0};
  object.pose.orientation = DirectX::XMFLOAT4{0.0f, 0.70710677f, 0.0f, 0.70710677f};
  object.poseTick = 9;
  return object;
}

[[nodiscard]] Outpost::FlightInputMessage SampleInput()
{
  Outpost::FlightInputMessage input;
  input.tick = 1'234;
  input.ship = 7;
  input.command.manoeuvre = Outpost::Manoeuvre::GoTo(Outpost::UniversePosition{1.0e8 + 0.5, -20.0, 3.25}, 45.0f);
  input.command.wishes.throttle = 0.75f;
  input.command.wishes.steering = DirectX::XMFLOAT3{-0.25f, 0.5f, 1.0f};
  input.command.wishes.strafe = Outpost::StrafeDirection::Down;
  input.command.wishes.thruster = true;
  input.command.wishes.cruise = true;
  input.command.wishes.engineOn = false;
  return input;
}

[[nodiscard]] Outpost::OwnShipMessage SampleOwnShip()
{
  Outpost::OwnShipMessage own;
  own.tick = 99;
  own.ship = 7;
  own.state.position = Outpost::UniversePosition{-1.0e7 + 0.125, 4.0, 8.0};
  own.state.orientation = DirectX::XMFLOAT4{0.0f, 0.70710677f, 0.0f, 0.70710677f};
  own.state.velocityMetresPerSecond = DirectX::XMFLOAT3{1.5f, -2.5f, 80.0f};
  own.state.angularVelocityRadiansPerSecond = DirectX::XMFLOAT3{0.1f, 0.2f, -0.3f};
  own.state.powerPool = 612.5f;
  own.state.thrustPool = 17.0f;
  own.state.thrusterActive = true;
  own.state.cruise = Outpost::CruiseState::Cooldown;
  own.state.cruiseTimerTicks = -140;
  own.state.cruiseRamp = 0.27f;
  return own;
}

[[nodiscard]] Outpost::DamageEventMessage SampleCarve()
{
  Outpost::DamageEventMessage damage;
  damage.event.target = 12;
  damage.event.kind = Outpost::MaskEventKind::Carve;
  damage.event.carve = Neuron::CarveSpec{41, {10, -2, 7}, {-64, 3, 12}, Neuron::CarveShape::Capsule, 2, 6, 400, 0xDEADBEEFu};
  damage.event.toughened = true;
  damage.event.cause = Outpost::DamageCause::Weapon;
  damage.event.inflictor = 7;
  return damage;
}

[[nodiscard]] Outpost::DamageEventMessage SamplePatch()
{
  Outpost::DamageEventMessage damage;
  damage.event.target = 7;
  damage.event.kind = Outpost::MaskEventKind::Patch;
  damage.event.patch = Neuron::PatchSpec{42, 120};
  damage.event.cause = Outpost::DamageCause::Repair;
  damage.event.inflictor = 7;
  return damage;
}

[[nodiscard]] std::vector<Outpost::Message> OneOfEach()
{
  return {Outpost::TickMessage{123'456'789'012ull},
          SampleShip(),
          Outpost::ObjectiveMessage{2, {"Escort tug T-041 to dock", true}},
          Outpost::ResourceMessage{77, 1, {"Ore", -5, 60}},
          Outpost::ObjectSpawnMessage{SampleObject()},
          Outpost::ObjectStateMessage{10, 12, SampleObject().pose},
          SampleInput(),
          SampleOwnShip(),
          Outpost::ShipMotionMessage{{7, 50, 0.4f, 33.0f, true, false, Outpost::CruiseState::Charging, 55}},
          Outpost::DockCommandMessage{88, 7, Outpost::DockCommand::Dock, 3},
          Outpost::DockStatusMessage{
            7, {90, Outpost::DockPhase::Docked, Outpost::DockAnswer::LaunchRefused, "Shipyard", "the ship has no power plant"}},
          Outpost::ObjectRemovedMessage{91, 12},
          Outpost::UseItemMessage{92, 7, Outpost::ItemKind::RepairKit},
          Outpost::MaskSnapshotMessage{12, 40, Outpost::MaskReference::Empty, {{3, 4}, {100, 1}, {4'000'000, 17}}},
          SampleCarve(),
          SamplePatch(),
          Outpost::BoltFiredMessage{93, 5, 7, Outpost::UniversePosition{1.0e8 + 0.5, -1.0, 2.0}, {0.0f, 600.0f, -3.5f}, 60},
          Outpost::BoltHitMessage{94, 5, 12, Outpost::UniversePosition{1.0e8 + 0.25, 30.0, 2.0}, true, false}};
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
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::Ship) == Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_UNIVERSE});
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::Objective) ==
                   Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_UNIVERSE});
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::ObjectSpawn) ==
                   Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_UNIVERSE});
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::ObjectState) ==
                   Outpost::Route{Neuron::Delivery::Datagram, Outpost::STREAM_UNIVERSE});
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
    const std::vector<Outpost::Message> largest{
      Outpost::TickMessage{~0ull}, Outpost::ResourceMessage{~0ull, 0xFFFF, {longName, -1, -1}},
      Outpost::ObjectStateMessage{~0ull, ~0u, SampleObject().pose}, SampleInput(),
      Outpost::ShipMotionMessage{{~0u, ~0ull, 1.0f, 1000.0f, true, true, Outpost::CruiseState::Cooldown}}};
    for (const Outpost::Message& message : largest)
    {
      Assert::IsTrue(Outpost::RouteOf(Outpost::KindOf(message)).delivery == Neuron::Delivery::Datagram);
      Assert::IsTrue(Outpost::Encode(message).size() <= Neuron::LoopbackTransport::MAX_DATAGRAM_BYTES);
    }
  }

  TEST_METHOD(AnOutOfRangeSwitchOrStateIsRefused)
  {
    // A flight input's switches, five bits, come just before its aim point, three doubles.
    std::vector<std::uint8_t> input = Outpost::Encode(SampleInput());
    input[input.size() - 25] = 32;
    AssertError(input, Outpost::DecodeError::Malformed, L"a sixth switch bit");
    // A ship motion's cruise state comes just before its shield share, four bytes.
    std::vector<std::uint8_t> motion =
      Outpost::Encode(Outpost::ShipMotionMessage{{1, 1, 0.0f, 0.0f, false, true, Outpost::CruiseState::Off}});
    motion[motion.size() - 5] = 4;
    AssertError(motion, Outpost::DecodeError::Malformed, L"cruise state 4 is not in the catalogue");
    // A dock command's command comes just before the targeted station, four bytes.
    std::vector<std::uint8_t> dock = Outpost::Encode(Outpost::DockCommandMessage{1, 7, Outpost::DockCommand::Dock});
    dock[dock.size() - 5] = 3;
    AssertError(dock, Outpost::DecodeError::Malformed, L"dock command 3 is not in the catalogue");
  }

  TEST_METHOD(FireAndAimTravelWithTheFlightInput)
  {
    Outpost::FlightInputMessage input = SampleInput();
    input.command.wishes.fire = true;
    input.command.aim = Outpost::UniversePosition{1.0e5 / 3.0, -12.5, 7.0};
    const auto decoded = Outpost::Decode(Outpost::Encode(input));
    Assert::IsTrue(decoded.has_value());
    Assert::IsTrue(std::get<Outpost::FlightInputMessage>(*decoded) == input, L"the fire switch and the aim point, exactly");
  }

  TEST_METHOD(TheOwnShipsShieldAndLauncherClocksTravelExactly)
  {
    Outpost::OwnShipMessage own;
    own.tick = 40;
    own.ship = 7;
    own.state.shield = 512.25f;
    own.state.shieldUp = false;
    own.state.shieldRebuildTicks = 431;
    own.state.launcherClockTicks = {0, 3, 17, Outpost::LAUNCHER_CLOCK_READY_TICKS};
    const auto decoded = Outpost::Decode(Outpost::Encode(own));
    Assert::IsTrue(decoded.has_value());
    Assert::IsTrue(std::get<Outpost::OwnShipMessage>(*decoded) == own);
  }

  TEST_METHOD(ARemovedObjectLeavesTheClientWithItsShipAndMotion)
  {
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::ObjectRemoved) ==
                   Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_UNIVERSE});
    Outpost::UniverseSnapshot universe;
    Outpost::UniverseObject raider;
    raider.id = 54;
    raider.name = "Raider";
    Outpost::ApplyMessage(Outpost::ObjectSpawnMessage{raider}, universe);
    Outpost::ShipState ship;
    ship.object = 54;
    ship.instance = "Raider";
    Outpost::ApplyMessage(Outpost::ShipMessage{ship}, universe);
    Outpost::ApplyMessage(Outpost::ShipMotionMessage{{54, 3, 0.5f, 10.0f, false, true, Outpost::CruiseState::Off}}, universe);
    const std::uint64_t revision = universe.objectsRevision;
    const auto decoded = Outpost::Decode(Outpost::Encode(Outpost::ObjectRemovedMessage{90, 54}));
    Assert::IsTrue(decoded.has_value());
    Outpost::ApplyMessage(*decoded, universe);
    Assert::IsTrue(universe.objects.empty() && universe.ships.empty() && universe.motions.empty());
    Assert::AreEqual(revision + 1, universe.objectsRevision, L"presentation rebuilds its instances");
    Outpost::ApplyMessage(*decoded, universe);
    Assert::AreEqual(revision + 1, universe.objectsRevision, L"a second removal of the same object changes nothing");
  }

  TEST_METHOD(MasksTheirEventsAndBoltsAreReliableFacts)
  {
    for (const Outpost::MessageKind kind : {Outpost::MessageKind::MaskSnapshot, Outpost::MessageKind::DamageEvent,
                                            Outpost::MessageKind::BoltFired, Outpost::MessageKind::BoltHit})
    {
      Assert::IsTrue(Outpost::RouteOf(kind) == Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_UNIVERSE});
    }
    // They change nothing in the snapshot: the client's combat picture takes them.
    Outpost::UniverseSnapshot universe;
    Outpost::ApplyMessage(SampleCarve(), universe);
    Outpost::ApplyMessage(Outpost::MaskSnapshotMessage{12, 1, Outpost::MaskReference::Full, {}}, universe);
    Assert::IsTrue(universe.objects.empty() && universe.objectsRevision == 0);
  }

  TEST_METHOD(ASnapshotWithMoreSpansThanItsBytesIsRefused)
  {
    std::vector<std::uint8_t> bytes = Outpost::Encode(Outpost::MaskSnapshotMessage{12, 40, Outpost::MaskReference::Full, {{3, 4}}});
    // The span count sits after the object (4), the last event (8) and the base (1).
    const std::size_t countAt = Outpost::MESSAGE_HEADER_BYTES + 13;
    bytes[countAt] = 0xFF;
    bytes[countAt + 1] = 0xFF;
    AssertError(bytes, Outpost::DecodeError::Malformed, L"a span count beyond the payload");
  }

  TEST_METHOD(UsingAnItemIsAReliableCommand)
  {
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::UseItem) == Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_COMMANDS});
    const Outpost::UseItemMessage use{12, 7, Outpost::ItemKind::RepairKit};
    std::vector<std::uint8_t> bytes = Outpost::Encode(use);
    const auto decoded = Outpost::Decode(bytes);
    Assert::IsTrue(decoded.has_value() && std::get<Outpost::UseItemMessage>(*decoded) == use);
    bytes.back() = 1;
    AssertError(bytes, Outpost::DecodeError::Malformed, L"item 1 is not in the catalogue");
  }

  TEST_METHOD(DockingTravelsReliablyAndTheClientKeepsItsStatus)
  {
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::DockCommand) ==
                   Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_COMMANDS});
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::DockStatus) ==
                   Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_UNIVERSE});
    Outpost::UniverseSnapshot universe;
    const Outpost::DockStatusMessage status{7, {90, Outpost::DockPhase::Final, Outpost::DockAnswer::None, "Shipyard", ""}};
    Outpost::ApplyMessage(status, universe);
    Assert::IsTrue(universe.ownDock == status.status);
    Assert::AreEqual(std::uint64_t{1}, universe.ownDockRevision);
  }

  TEST_METHOD(FlightInputTravelsAsACommandDatagram)
  {
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::FlightInput) ==
                   Outpost::Route{Neuron::Delivery::Datagram, Outpost::STREAM_COMMANDS});
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::OwnShip) == Outpost::Route{Neuron::Delivery::Reliable, Outpost::STREAM_UNIVERSE});
    Assert::IsTrue(Outpost::RouteOf(Outpost::MessageKind::ShipMotion).delivery == Neuron::Delivery::Datagram);
  }

  TEST_METHOD(TheOwnShipAndTheNewestMotionAreKept)
  {
    Outpost::UniverseSnapshot universe;
    Outpost::ApplyMessage(SampleOwnShip(), universe);
    Assert::AreEqual(std::uint32_t{7}, universe.ownShip);
    Assert::IsTrue(universe.ownShipState == SampleOwnShip().state);
    Assert::AreEqual(std::uint64_t{1}, universe.ownShipRevision);

    Outpost::ApplyMessage(Outpost::ShipMotionMessage{{7, 50, 0.4f, 33.0f, false, true, Outpost::CruiseState::Off}}, universe);
    Outpost::ApplyMessage(Outpost::ShipMotionMessage{{7, 49, 1.0f, 80.0f, false, true, Outpost::CruiseState::Off}}, universe);
    Assert::AreEqual(std::size_t{1}, universe.motions.size());
    Assert::AreEqual(0.4f, universe.FindMotion(7)->throttle, L"an older engine state is dropped");
  }

  TEST_METHOD(ASpawnAddsAnObjectAndRaisesTheRevision)
  {
    Outpost::UniverseSnapshot universe;
    Outpost::ApplyMessage(Outpost::ObjectSpawnMessage{SampleObject()}, universe);
    Assert::AreEqual(std::size_t{1}, universe.objects.size());
    Assert::IsTrue(universe.objects[0] == SampleObject());
    Assert::AreEqual(std::uint64_t{1}, universe.objectsRevision);
  }

  TEST_METHOD(OnlyANewerPoseMovesAnObject)
  {
    Outpost::UniverseSnapshot universe;
    Outpost::ApplyMessage(Outpost::ObjectSpawnMessage{SampleObject()}, universe); // pose from tick 9
    Outpost::Pose moved = SampleObject().pose;
    moved.position.y += 10.0;

    Outpost::ApplyMessage(Outpost::ObjectStateMessage{8, 12, moved}, universe);
    Assert::IsTrue(universe.objects[0].pose == SampleObject().pose, L"an older pose is dropped");

    Outpost::ApplyMessage(Outpost::ObjectStateMessage{11, 12, moved}, universe);
    Assert::IsTrue(universe.objects[0].pose == moved);
    Assert::AreEqual(std::uint64_t{11}, universe.objects[0].poseTick);
    Assert::AreEqual(std::uint64_t{1}, universe.objectsRevision, L"a move is not a spawn: nothing is rebuilt");
    Assert::AreEqual(std::uint64_t{1}, universe.posesRevision);

    Outpost::ApplyMessage(Outpost::ObjectStateMessage{12, 12, moved}, universe);
    Assert::AreEqual(std::uint64_t{12}, universe.objects[0].poseTick, L"the same pose, newer, still advances the tick");
    Assert::AreEqual(std::uint64_t{1}, universe.posesRevision, L"... without a move");
  }

  TEST_METHOD(StateForAnUnknownObjectChangesNothing)
  {
    Outpost::UniverseSnapshot universe;
    Outpost::ApplyMessage(Outpost::ObjectStateMessage{5, 99, SampleObject().pose}, universe);
    Assert::IsTrue(universe.objects.empty());
    Assert::AreEqual(std::uint64_t{0}, universe.objectsRevision);
  }

  TEST_METHOD(AnOlderResourceIsDropped)
  {
    Outpost::UniverseSnapshot universe;
    Outpost::ApplyMessage(Outpost::ResourceMessage{20, 1, {"Ore", 50, 60}}, universe);
    Outpost::ApplyMessage(Outpost::ResourceMessage{19, 1, {"Ore", 40, 60}}, universe);
    Assert::AreEqual(std::size_t{2}, universe.resources.size(), L"index 1 makes room for index 0");
    Assert::AreEqual(std::int64_t{50}, universe.resources[1].amount);
  }

  TEST_METHOD(AShipIsKeyedByItsObject)
  {
    Outpost::UniverseSnapshot universe;
    Outpost::ShipMessage ship = SampleShip();
    ship.ship.object = 4;
    Outpost::ApplyMessage(ship, universe);
    ship.ship.hullPercent = 12;
    Outpost::ApplyMessage(ship, universe);
    Assert::AreEqual(std::size_t{1}, universe.ships.size());
    Assert::AreEqual(12, universe.ships[0].hullPercent);
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
