#include "TestSupport.h"

#include "Palette.h"
#include "VoxFile.h"

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

TEST_CLASS(PaletteTests)
{
public:
  TEST_METHOD(IndexZeroIsEmpty)
  {
    Assert::IsTrue(Outpost::CanonicalPalette()[0] == (Neuron::Rgba8{0, 0, 0, 0}));
  }

  TEST_METHOD(OnlyBlueGlazingIsTranslucent)
  {
    const Neuron::VoxPalette& palette = Outpost::CanonicalPalette();
    for (std::size_t i = 1; i < palette.size(); ++i)
    {
      Assert::AreEqual(i == Outpost::PaletteIndex::WINDOW_BLUE ? 80 : 255, static_cast<int>(palette[i].a));
    }
  }

  TEST_METHOD(EmissionLivesOnlyOnEmissiveIndices)
  {
    namespace Pal = Outpost::PaletteIndex;
    const Neuron::VoxelMaterialTable& materials = Outpost::MaterialTable();
    for (std::size_t i = 0; i < materials.size(); ++i)
    {
      const bool emits = materials[i].emissionR > 0.0f || materials[i].emissionG > 0.0f || materials[i].emissionB > 0.0f;
      const bool emissiveRole = i >= Pal::LAMP_AMBER && i <= Pal::STROBE_YELLOW;
      Assert::AreEqual(emissiveRole, emits, (L"palette index " + std::to_wstring(i)).c_str());
      Assert::AreEqual(i == Pal::STROBE_YELLOW ? 1.5f : 0.0f, materials[i].strobePeriodSeconds);
    }
    Assert::IsTrue(materials[Pal::LAMP_AMBER].glowRadius > 0.0f, L"lamps carry a glow sprite");
    Assert::IsTrue(materials[Pal::WINDOW_AMBER].glowRadius == 0.0f, L"windows emit without a sprite");
    Assert::IsTrue(materials[Pal::STROBE_YELLOW].glowRadius > 0.0f, L"strobe halos flash with their emitting cells");
    Assert::IsTrue(materials[Pal::WINDOW_BLUE].emissionB == 0.0f, L"blue glazing is not an opaque glowing display");
  }

  TEST_METHOD(EveryShippedModelCarriesTheCanonicalPaletteChunk)
  {
    const std::vector<std::uint8_t> expected = Neuron::PaletteToRgbaChunk(Outpost::CanonicalPalette());
    const std::filesystem::path models = AssetRoot() / "Models";
    std::size_t count = 0;
    std::vector<std::uint8_t> first;
    for (const auto& entry : std::filesystem::directory_iterator(models))
    {
      if (entry.path().extension() != ".vox")
      {
        continue;
      }
      Neuron::VoxDocument document;
      Assert::IsTrue(Neuron::LoadVoxFile(entry.path(), document) == Neuron::VoxError::None, entry.path().c_str());
      Assert::IsTrue(document.hasPalette, entry.path().c_str());
      Assert::IsTrue(document.rgbaChunk == expected, entry.path().c_str());
      Assert::AreEqual(std::size_t{2}, document.materials.size(), L"new authoring materials travel with every palette");
      Assert::AreEqual(std::uint32_t{Outpost::PaletteIndex::STROBE_YELLOW}, document.materials[0].colorIndex);
      Assert::IsTrue(document.materials[0].properties.front() == std::pair<std::string, std::string>{"_type", "_emit"});
      Assert::AreEqual(std::uint32_t{Outpost::PaletteIndex::WINDOW_BLUE}, document.materials[1].colorIndex);
      Assert::IsTrue(document.materials[1].properties.front() == std::pair<std::string, std::string>{"_type", "_glass"});
      if (first.empty())
      {
        first = document.rgbaChunk;
      }
      Assert::IsTrue(document.rgbaChunk == first, L"every file's RGBA chunk is byte-identical");
      ++count;
    }
    Assert::IsTrue(count >= 26, L"all MVP models are present");
  }

  TEST_METHOD(ShippedModelsRespectTheDimensionLimit)
  {
    for (const auto& entry : std::filesystem::directory_iterator(AssetRoot() / "Models"))
    {
      Neuron::VoxDocument document;
      Assert::IsTrue(Neuron::LoadVoxFile(entry.path(), document) == Neuron::VoxError::None);
      Assert::IsTrue(document.model.SizeX() <= Neuron::VOX_MAX_DIMENSION && document.model.SizeY() <= Neuron::VOX_MAX_DIMENSION &&
                     document.model.SizeZ() <= Neuron::VOX_MAX_DIMENSION);
      Assert::IsTrue(document.model.CountSolid() > 0, entry.path().c_str());
    }
  }
};

} // namespace GameCoreTests
