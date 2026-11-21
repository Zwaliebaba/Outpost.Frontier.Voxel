#include "TestSupport.h"

#include "VoxFile.h"

#include <algorithm>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

// A hand-authored two-voxel file, written byte by byte rather than by WriteVox, with a palette
// whose every entry is distinct: chunk entry k is (k, 255 - k, k / 2, 200).
[[nodiscard]] std::vector<std::uint8_t> HandAuthoredFixture()
{
  ByteWriter w;
  w.Tag("VOX ");
  w.Int(150);
  const std::int32_t sizeChunk = 12 + 12;
  const std::int32_t xyziChunk = 12 + 4 + 2 * 4;
  const std::int32_t rgbaChunk = 12 + 1024;
  w.Tag("MAIN");
  w.Int(0);
  w.Int(sizeChunk + xyziChunk + rgbaChunk);
  w.Tag("SIZE");
  w.Int(12);
  w.Int(0);
  w.Int(3);
  w.Int(2);
  w.Int(1);
  w.Tag("XYZI");
  w.Int(4 + 2 * 4);
  w.Int(0);
  w.Int(2);
  for (const std::uint8_t byte : std::initializer_list<std::uint8_t>{0, 0, 0, 1, 2, 1, 0, 255})
  {
    w.Byte(byte);
  }
  w.Tag("RGBA");
  w.Int(1024);
  w.Int(0);
  for (int k = 0; k < 256; ++k)
  {
    w.Byte(static_cast<std::uint8_t>(k));
    w.Byte(static_cast<std::uint8_t>(255 - k));
    w.Byte(static_cast<std::uint8_t>(k / 2));
    w.Byte(200);
  }
  return w.Bytes();
}

// The minimal valid file with the SIZE and XYZI contents supplied by the caller.
[[nodiscard]] std::vector<std::uint8_t> MinimalFile(std::int32_t _x, std::int32_t _y, std::int32_t _z,
                                                    const std::vector<std::uint8_t>& _voxels)
{
  ByteWriter w;
  w.Tag("VOX ");
  w.Int(150);
  const auto voxelBytes = static_cast<std::int32_t>(_voxels.size());
  w.Tag("MAIN");
  w.Int(0);
  w.Int(24 + 12 + 4 + voxelBytes);
  w.Tag("SIZE");
  w.Int(12);
  w.Int(0);
  w.Int(_x);
  w.Int(_y);
  w.Int(_z);
  w.Tag("XYZI");
  w.Int(4 + voxelBytes);
  w.Int(0);
  w.Int(voxelBytes / 4);
  for (const std::uint8_t byte : _voxels)
  {
    w.Byte(byte);
  }
  return w.Bytes();
}

} // namespace

TEST_CLASS(VoxFileTests)
{
public:
  TEST_METHOD(WriteThenReadRoundTripsModelAndPalette)
  {
    Neuron::VoxModel model(5, 4, 3);
    model.Set(0, 0, 0, 1);
    model.Set(4, 3, 2, 255);
    model.Set(2, 1, 1, 77);
    Neuron::VoxPalette palette{};
    for (std::size_t i = 1; i < palette.size(); ++i)
    {
      palette[i] = Neuron::Rgba8{static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i * 3), static_cast<std::uint8_t>(255 - i), 255};
    }
    const std::vector<std::uint8_t> bytes = Neuron::WriteVox(model, palette);
    Neuron::VoxDocument document;
    Assert::IsTrue(Neuron::ReadVox(bytes, document) == Neuron::VoxError::None);
    Assert::IsTrue(document.model == model);
    Assert::IsTrue(document.hasPalette);
    Assert::IsTrue(document.palette == palette);
    Assert::IsTrue(Neuron::WriteVox(document.model, document.palette) == bytes, L"writing is deterministic");
  }

  TEST_METHOD(HandAuthoredFixtureAppliesThePaletteOffset)
  {
    Neuron::VoxDocument document;
    Assert::IsTrue(Neuron::ReadVox(HandAuthoredFixture(), document) == Neuron::VoxError::None);
    Assert::AreEqual(3u, document.model.SizeX());
    Assert::AreEqual(2u, document.model.SizeY());
    Assert::AreEqual(1u, document.model.SizeZ());
    Assert::AreEqual(1, static_cast<int>(document.model.At(0, 0, 0)));
    Assert::AreEqual(255, static_cast<int>(document.model.At(2, 1, 0)));
    Assert::AreEqual(std::size_t{2}, document.model.CountSolid());
    // Voxel colour c is chunk entry c - 1.
    Assert::IsTrue(document.palette[1] == (Neuron::Rgba8{0, 255, 0, 200}));
    Assert::IsTrue(document.palette[2] == (Neuron::Rgba8{1, 254, 0, 200}));
    Assert::IsTrue(document.palette[255] == (Neuron::Rgba8{254, 1, 127, 200}));
    Assert::IsTrue(document.palette[0] == (Neuron::Rgba8{0, 0, 0, 0}), L"index 0 is empty");
    Assert::AreEqual(Neuron::VOX_RGBA_CHUNK_BYTES, document.rgbaChunk.size());
    const std::vector<std::uint8_t> chunk = Neuron::PaletteToRgbaChunk(document.palette);
    Assert::IsTrue(std::equal(chunk.begin(), chunk.begin() + 1020, document.rgbaChunk.begin()),
                   L"palette and chunk agree on every entry a voxel can address");
  }

  TEST_METHOD(RejectsBadMagic)
  {
    std::vector<std::uint8_t> bytes = HandAuthoredFixture();
    bytes[0] = 'X';
    Neuron::VoxDocument document;
    Assert::IsTrue(Neuron::ReadVox(bytes, document) == Neuron::VoxError::BadMagic);
  }

  TEST_METHOD(RejectsTruncatedChunks)
  {
    const std::vector<std::uint8_t> full = HandAuthoredFixture();
    for (const std::size_t keep : {std::size_t{10}, std::size_t{20}, std::size_t{30}, std::size_t{50}, full.size() - 100})
    {
      const std::vector<std::uint8_t> bytes(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(keep));
      Neuron::VoxDocument document;
      const Neuron::VoxError error = Neuron::ReadVox(bytes, document);
      Assert::IsTrue(error == Neuron::VoxError::TruncatedChunk || error == Neuron::VoxError::MissingMainChunk, L"truncation is reported");
    }
    // A chunk that claims more content than the file holds.
    std::vector<std::uint8_t> lying = MinimalFile(2, 2, 2, {0, 0, 0, 1});
    lying[8 + 12 + 4] = 0xff; // SIZE content length
    Neuron::VoxDocument document;
    Assert::IsTrue(Neuron::ReadVox(lying, document) == Neuron::VoxError::TruncatedChunk);
  }

  TEST_METHOD(RejectsInvalidDimensions)
  {
    Neuron::VoxDocument document;
    Assert::IsTrue(Neuron::ReadVox(MinimalFile(0, 4, 4, {}), document) == Neuron::VoxError::InvalidDimensions);
    Assert::IsTrue(Neuron::ReadVox(MinimalFile(4, 257, 4, {}), document) == Neuron::VoxError::InvalidDimensions);
    Assert::IsTrue(Neuron::ReadVox(MinimalFile(4, 4, -3, {}), document) == Neuron::VoxError::InvalidDimensions);
    Assert::IsTrue(Neuron::ReadVox(MinimalFile(256, 256, 256, {255, 255, 255, 9}), document) == Neuron::VoxError::None,
                   L"256 is the largest legal dimension");
  }

  TEST_METHOD(RejectsOutOfBoundsVoxels)
  {
    Neuron::VoxDocument document;
    Assert::IsTrue(Neuron::ReadVox(MinimalFile(2, 2, 2, {2, 0, 0, 1}), document) == Neuron::VoxError::VoxelOutOfBounds);
    Assert::IsTrue(Neuron::ReadVox(MinimalFile(2, 2, 2, {0, 0, 5, 1}), document) == Neuron::VoxError::VoxelOutOfBounds);
    Assert::IsTrue(Neuron::ReadVox(MinimalFile(2, 2, 2, {1, 1, 1, 1}), document) == Neuron::VoxError::None);
  }

  TEST_METHOD(WriterHonoursTheDimensionLimit)
  {
    Neuron::VoxModel model(256, 1, 1);
    model.Set(255, 0, 0, 3);
    model.Set(256, 0, 0, 3); // outside the grid: ignored
    Assert::AreEqual(std::size_t{1}, model.CountSolid());
    Neuron::VoxDocument document;
    Assert::IsTrue(Neuron::ReadVox(Neuron::WriteVox(model, Neuron::VoxPalette{}), document) == Neuron::VoxError::None);
    Assert::AreEqual(3, static_cast<int>(document.model.At(255, 0, 0)));
  }
};

} // namespace NeuronCoreTests
