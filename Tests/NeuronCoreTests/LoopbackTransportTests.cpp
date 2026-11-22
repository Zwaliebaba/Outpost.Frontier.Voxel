#include "LoopbackTransport.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

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

TEST_CLASS(LoopbackTransportTests)
{
public:
  TEST_METHOD(PacketsArriveInTheOrderSent)
  {
    Neuron::LoopbackTransport transport;
    for (std::uint8_t i = 0; i < 5; ++i)
    {
      const std::vector<std::uint8_t> bytes{i};
      Assert::IsTrue(transport.Send(Neuron::Peer::Server, i % 2 == 0 ? Neuron::Delivery::Reliable : Neuron::Delivery::Datagram, 0, bytes));
    }
    for (std::uint8_t i = 0; i < 5; ++i)
    {
      Assert::IsTrue(i == Next(transport, Neuron::Peer::Client).bytes.at(0));
    }
    Assert::IsFalse(transport.Receive(Neuron::Peer::Client).has_value());
  }

  TEST_METHOD(TheTwoDirectionsAreSeparate)
  {
    Neuron::LoopbackTransport transport;
    const std::vector<std::uint8_t> up{1};
    const std::vector<std::uint8_t> down{2};
    Assert::IsTrue(transport.Send(Neuron::Peer::Client, Neuron::Delivery::Reliable, 3, up));
    Assert::IsTrue(transport.Send(Neuron::Peer::Server, Neuron::Delivery::Reliable, 0, down));
    Assert::AreEqual(std::size_t{1}, transport.Pending(Neuron::Peer::Server));
    Assert::AreEqual(std::size_t{1}, transport.Pending(Neuron::Peer::Client));
    const Neuron::Packet atServer = Next(transport, Neuron::Peer::Server);
    Assert::IsTrue(std::uint8_t{1} == atServer.bytes.at(0));
    Assert::IsTrue(std::uint16_t{3} == atServer.stream, L"the stream travels with the packet");
  }

  TEST_METHOD(AnOversizedDatagramIsRefusedLikeANetworkWould)
  {
    Neuron::LoopbackTransport transport;
    const std::vector<std::uint8_t> fits(Neuron::LoopbackTransport::MAX_DATAGRAM_BYTES);
    const std::vector<std::uint8_t> tooBig(Neuron::LoopbackTransport::MAX_DATAGRAM_BYTES + 1);
    Assert::IsTrue(transport.Send(Neuron::Peer::Server, Neuron::Delivery::Datagram, 0, fits));
    Assert::IsFalse(transport.Send(Neuron::Peer::Server, Neuron::Delivery::Datagram, 0, tooBig));
    Assert::IsTrue(transport.Send(Neuron::Peer::Server, Neuron::Delivery::Reliable, 0, tooBig), L"a stream has no such limit");
    Assert::AreEqual(std::size_t{2}, transport.Pending(Neuron::Peer::Client));
  }
};

} // namespace NeuronCoreTests
