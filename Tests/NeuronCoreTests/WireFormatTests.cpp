#include "WireFormat.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

TEST_CLASS(WireFormatTests)
{
public:
  TEST_METHOD(IntegersAreLittleEndianWhateverTheHost)
  {
    Neuron::WireWriter writer;
    writer.Uint32(0x01020304u);
    writer.Uint16(0xA0B0u);
    const std::vector<std::uint8_t> expected{0x04, 0x03, 0x02, 0x01, 0xB0, 0xA0};
    Assert::IsTrue(writer.Bytes() == expected);
  }

  TEST_METHOD(EveryWidthRoundTrips)
  {
    Neuron::WireWriter writer;
    writer.Uint8(0xFE);
    writer.Uint16(0xBEEF);
    writer.Uint32(0xDEADBEEFu);
    writer.Uint64(0x0123456789ABCDEFull);
    writer.Int32(-123'456);
    writer.Int64(std::numeric_limits<std::int64_t>::min());
    writer.Float32(-1.5f);
    writer.Float64(100'000.000'000'1);
    writer.String("Kessler Reach");
    Assert::IsTrue(writer.Ok());

    const std::vector<std::uint8_t> bytes = writer.TakeBytes();
    Neuron::WireReader reader(bytes);
    Assert::IsTrue(std::uint8_t{0xFE} == reader.Uint8());
    Assert::IsTrue(std::uint16_t{0xBEEF} == reader.Uint16());
    Assert::AreEqual(std::uint32_t{0xDEADBEEFu}, reader.Uint32());
    Assert::AreEqual(std::uint64_t{0x0123456789ABCDEFull}, reader.Uint64());
    Assert::AreEqual(std::int32_t{-123'456}, reader.Int32());
    Assert::IsTrue(reader.Int64() == std::numeric_limits<std::int64_t>::min());
    Assert::IsTrue(-1.5f == reader.Float32());
    Assert::IsTrue(reader.Float64() == 100'000.000'000'1, L"a double crosses bit for bit");
    Assert::IsTrue(std::string("Kessler Reach") == reader.String());
    Assert::IsTrue(reader.Ok());
    Assert::AreEqual(std::size_t{0}, reader.Remaining());
  }

  TEST_METHOD(ReadingPastTheEndFailsAndYieldsZero)
  {
    const std::vector<std::uint8_t> bytes{0x01, 0x02, 0x03};
    Neuron::WireReader reader(bytes);
    Assert::AreEqual(std::uint32_t{0}, reader.Uint32());
    Assert::IsFalse(reader.Ok());
    Assert::IsTrue(std::uint8_t{0} == reader.Uint8(), L"a failed reader stays failed");
  }

  TEST_METHOD(AStringLongerThanItsBytesFails)
  {
    const std::vector<std::uint8_t> bytes{0x05, 0x00, 'a', 'b'};
    Neuron::WireReader reader(bytes);
    Assert::IsTrue(reader.String().empty());
    Assert::IsFalse(reader.Ok());
  }

  TEST_METHOD(AStringTooLongForTheWireIsRefused)
  {
    Neuron::WireWriter writer;
    writer.String(std::string(Neuron::WIRE_MAX_STRING_BYTES + 1, 'x'));
    Assert::IsFalse(writer.Ok());
    Assert::IsTrue(writer.Bytes().empty());
  }
};

} // namespace NeuronCoreTests
