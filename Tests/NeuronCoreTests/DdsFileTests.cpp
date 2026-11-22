#include "DdsFile.h"

#include <cstdint>
#include <string>
#include <vector>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

// File offsets of the fields the refusal tests rewrite (the header starts after the 4-byte magic).
constexpr std::size_t MIP_COUNT_AT = 4 + 24;
constexpr std::size_t FOUR_CC_AT = 4 + 80;
constexpr std::size_t CAPS2_AT = 4 + 108;
constexpr std::size_t DX10_FORMAT_AT = 128;
constexpr std::size_t DX10_DIMENSION_AT = 132;
constexpr std::size_t DX10_MISC_AT = 136;
constexpr std::size_t DX10_ARRAY_AT = 140;
constexpr std::size_t DX10_ALPHA_AT = 144;

[[nodiscard]] Neuron::DdsImage Image(DXGI_FORMAT _format, std::uint32_t _width, std::uint32_t _height,
                                     Neuron::DdsAlphaMode _alpha = Neuron::DdsAlphaMode::Straight)
{
  Neuron::DdsImage image;
  image.format = _format;
  image.width = _width;
  image.height = _height;
  image.alphaMode = _alpha;
  image.payload.resize(static_cast<std::size_t>(Neuron::DdsPayloadBytes(_format, _width, _height)));
  for (std::size_t i = 0; i < image.payload.size(); ++i)
  {
    image.payload[i] = static_cast<std::uint8_t>(i * 7 + 3);
  }
  return image;
}

void Poke(std::vector<std::uint8_t>& _bytes, std::size_t _offset, std::uint32_t _value)
{
  for (std::size_t i = 0; i < 4; ++i)
  {
    _bytes[_offset + i] = static_cast<std::uint8_t>((_value >> (8 * i)) & 0xffu);
  }
}

/// Parses and expects a refusal whose reason contains _expected.
void ExpectRefused(const std::vector<std::uint8_t>& _bytes, const std::string& _expected)
{
  Neuron::DdsImage image;
  std::string reason;
  const bool parsed = Neuron::ParseDds(_bytes, image, reason);
  const std::wstring message = L"expected a refusal mentioning '" + std::wstring(_expected.begin(), _expected.end()) + L"', got '" +
                               std::wstring(reason.begin(), reason.end()) + L"'";
  Assert::IsFalse(parsed, message.c_str());
  Assert::IsTrue(reason.find(_expected) != std::string::npos, message.c_str());
}

} // namespace

TEST_CLASS(DdsFileTests)
{
public:
  TEST_METHOD(AFileOfEachSupportedFormatParses)
  {
    for (const DXGI_FORMAT format : {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                                     DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB})
    {
      const Neuron::DdsImage written = Image(format, 12, 8);
      Neuron::DdsImage read;
      std::string reason;
      Assert::IsTrue(Neuron::ParseDds(Neuron::WriteDds(written), read, reason), std::wstring(reason.begin(), reason.end()).c_str());
      Assert::IsTrue(read.format == format);
      Assert::AreEqual(12u, read.width);
      Assert::AreEqual(8u, read.height);
      Assert::IsTrue(read.alphaMode == Neuron::DdsAlphaMode::Straight);
      Assert::IsTrue(read.payload == written.payload, L"the payload comes back byte for byte");
    }
  }

  TEST_METHOD(PayloadSizesFollowTheFormat)
  {
    Assert::AreEqual(std::uint64_t{12} * 8, Neuron::DdsPayloadBytes(DXGI_FORMAT_R8_UNORM, 12, 8));
    Assert::AreEqual(std::uint64_t{12} * 8 * 4, Neuron::DdsPayloadBytes(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 12, 8));
    Assert::AreEqual(std::uint64_t{3} * 2 * 16, Neuron::DdsPayloadBytes(DXGI_FORMAT_BC7_UNORM, 12, 8));
    Assert::AreEqual(std::uint64_t{512} * 512, Neuron::DdsPayloadBytes(DXGI_FORMAT_BC7_UNORM_SRGB, 512, 512), L"one byte a texel");
  }

  TEST_METHOD(Bc7DimensionsRoundUpToWholeBlocks)
  {
    // 5 x 3 texels is 2 x 1 blocks; 1 x 1 is one block; 9 x 4 is 3 x 1.
    Assert::AreEqual(std::uint64_t{2} * 16, Neuron::DdsPayloadBytes(DXGI_FORMAT_BC7_UNORM, 5, 3));
    Assert::AreEqual(std::uint64_t{16}, Neuron::DdsPayloadBytes(DXGI_FORMAT_BC7_UNORM, 1, 1));
    Assert::AreEqual(std::uint64_t{3} * 16, Neuron::DdsPayloadBytes(DXGI_FORMAT_BC7_UNORM_SRGB, 9, 4));
    Assert::AreEqual(std::uint32_t{1}, Neuron::DdsRowCount(DXGI_FORMAT_BC7_UNORM, 3));

    const Neuron::DdsImage odd = Image(DXGI_FORMAT_BC7_UNORM_SRGB, 5, 3);
    Assert::AreEqual(std::size_t{32}, odd.payload.size());
    Neuron::DdsImage read;
    std::string reason;
    Assert::IsTrue(Neuron::ParseDds(Neuron::WriteDds(odd), read, reason), L"a 5 x 3 BC7 file with two blocks parses");
    Assert::AreEqual(5u, read.width);
    Assert::AreEqual(3u, read.height);

    std::vector<std::uint8_t> bytes = Neuron::WriteDds(odd);
    bytes.pop_back();
    ExpectRefused(bytes, "truncated payload");
    bytes.push_back(0);
    bytes.push_back(0);
    ExpectRefused(bytes, "over-long payload");
  }

  TEST_METHOD(ATruncatedHeaderIsRefused)
  {
    const std::vector<std::uint8_t> whole = Neuron::WriteDds(Image(DXGI_FORMAT_R8_UNORM, 4, 4));
    ExpectRefused(std::vector<std::uint8_t>(whole.begin(), whole.begin() + 60), "truncated header");
    ExpectRefused(std::vector<std::uint8_t>(whole.begin(), whole.begin() + 136), "truncated header");
    ExpectRefused(std::vector<std::uint8_t>(whole.begin(), whole.begin() + 2), "magic");
    std::vector<std::uint8_t> wrongMagic = whole;
    wrongMagic[0] = 'X';
    ExpectRefused(wrongMagic, "magic");
  }

  TEST_METHOD(TruncatedAndOverLongPayloadsAreRefused)
  {
    for (const DXGI_FORMAT format : {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_BC7_UNORM})
    {
      std::vector<std::uint8_t> bytes = Neuron::WriteDds(Image(format, 8, 8));
      std::vector<std::uint8_t> shorter(bytes.begin(), bytes.end() - 1);
      ExpectRefused(shorter, "truncated payload");
      bytes.push_back(0);
      ExpectRefused(bytes, "over-long payload");
    }
    // A header with no payload at all.
    const std::vector<std::uint8_t> bytes = Neuron::WriteDds(Image(DXGI_FORMAT_R8_UNORM, 8, 8));
    ExpectRefused(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + Neuron::DDS_PAYLOAD_OFFSET), "truncated payload");
  }

  TEST_METHOD(AFileWithoutTheDx10HeaderIsRefused)
  {
    std::vector<std::uint8_t> bytes = Neuron::WriteDds(Image(DXGI_FORMAT_BC7_UNORM, 8, 8));
    Poke(bytes, FOUR_CC_AT, 0x31545844u); // "DXT1"
    ExpectRefused(bytes, "missing DX10 header");
  }

  TEST_METHOD(AnUnsupportedFormatIsRefused)
  {
    std::vector<std::uint8_t> bytes = Neuron::WriteDds(Image(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8));
    Poke(bytes, DX10_FORMAT_AT, static_cast<std::uint32_t>(DXGI_FORMAT_B8G8R8A8_UNORM));
    ExpectRefused(bytes, "unsupported format 87");
    Poke(bytes, DX10_FORMAT_AT, static_cast<std::uint32_t>(DXGI_FORMAT_BC1_UNORM));
    ExpectRefused(bytes, "unsupported format");
  }

  TEST_METHOD(MoreThanOneMipLevelIsRefused)
  {
    std::vector<std::uint8_t> bytes = Neuron::WriteDds(Image(DXGI_FORMAT_R8_UNORM, 8, 8));
    Poke(bytes, MIP_COUNT_AT, 4);
    ExpectRefused(bytes, "mip count 4");
  }

  TEST_METHOD(ArraysCubesAndVolumesAreRefused)
  {
    const std::vector<std::uint8_t> valid = Neuron::WriteDds(Image(DXGI_FORMAT_R8_UNORM, 8, 8));
    std::vector<std::uint8_t> bytes = valid;
    Poke(bytes, DX10_ARRAY_AT, 6);
    ExpectRefused(bytes, "array size 6");
    bytes = valid;
    Poke(bytes, DX10_MISC_AT, 0x4u); // TEXTURECUBE
    ExpectRefused(bytes, "cube");
    bytes = valid;
    Poke(bytes, CAPS2_AT, 0x200u | 0xFC00u); // a legacy cube map with all six faces
    ExpectRefused(bytes, "cube");
    bytes = valid;
    Poke(bytes, DX10_DIMENSION_AT, 4); // TEXTURE3D
    ExpectRefused(bytes, "resource dimension 4");
    bytes = valid;
    Poke(bytes, CAPS2_AT, 0x200000u); // VOLUME
    ExpectRefused(bytes, "volume");
  }

  TEST_METHOD(ColorArtMustDeclareStraightOrOpaqueAlpha)
  {
    for (const DXGI_FORMAT format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_BC7_UNORM_SRGB})
    {
      std::vector<std::uint8_t> bytes = Neuron::WriteDds(Image(format, 8, 8, Neuron::DdsAlphaMode::Premultiplied));
      ExpectRefused(bytes, "alpha mode premultiplied");
      Poke(bytes, DX10_ALPHA_AT, static_cast<std::uint32_t>(Neuron::DdsAlphaMode::Unknown));
      ExpectRefused(bytes, "alpha mode unknown");
      Poke(bytes, DX10_ALPHA_AT, static_cast<std::uint32_t>(Neuron::DdsAlphaMode::Opaque));
      Neuron::DdsImage read;
      std::string reason;
      Assert::IsTrue(Neuron::ParseDds(bytes, read, reason), L"opaque colour art is accepted");
    }
    // R8 is coverage: it carries no colour to premultiply, so its alpha mode is not a refusal.
    Neuron::DdsImage read;
    std::string reason;
    Assert::IsTrue(Neuron::ParseDds(Neuron::WriteDds(Image(DXGI_FORMAT_R8_UNORM, 8, 8, Neuron::DdsAlphaMode::Unknown)), read, reason));
  }

  TEST_METHOD(AnEmptyOrOversizedTextureIsRefused)
  {
    std::vector<std::uint8_t> bytes = Neuron::WriteDds(Image(DXGI_FORMAT_R8_UNORM, 8, 8));
    Poke(bytes, 4 + 12, 0); // width
    ExpectRefused(bytes, "invalid size");
    Poke(bytes, 4 + 12, Neuron::DDS_MAX_DIMENSION + 1);
    ExpectRefused(bytes, "invalid size");
  }
};

} // namespace NeuronCoreTests
