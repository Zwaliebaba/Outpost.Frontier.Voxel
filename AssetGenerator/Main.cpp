// AssetGenerator: writes every scene model as an editable MagicaVoxel .vox file (ADR-002).
//
//   AssetGenerator.exe [--out <AssetsDirectory>] [--check]
//   AssetGenerator.exe --crowd <Directory>
//
// Without --out it finds the repository's Assets directory by walking up from the executable.
// Output is deterministic: the same source produces byte-identical files. --check regenerates in
// memory and fails if any file on disk differs, which is how a stale asset is caught. --crowd
// writes the crowd benchmark's generated scenes (plan task F6, CrowdScene.h), which are not shipped.

#include "CrowdScene.h"
#include "FileSys.h"
#include "ModelCatalog.h"
#include "Palette.h"
#include "VoxFile.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace
{

int g_moduleAnchor = 0;

[[nodiscard]] int Usage()
{
  std::fputs("usage: AssetGenerator [--out <AssetsDirectory>] [--check]\n       AssetGenerator --crowd <Directory>\n", stderr);
  return 2;
}

} // namespace

int wmain(int _argc, wchar_t** _argv)
{
  std::filesystem::path assetRoot;
  bool check = false;
  for (int i = 1; i < _argc; ++i)
  {
    const std::wstring_view argument = _argv[i];
    if (argument == L"--out" && i + 1 < _argc)
    {
      assetRoot = _argv[++i];
    }
    else if (argument == L"--check")
    {
      check = true;
    }
    else if (argument == L"--crowd" && i + 1 < _argc)
    {
      return AssetGen::WriteCrowdScenes(_argv[++i]) == 0 ? 0 : 1;
    }
    else
    {
      return Usage();
    }
  }
  if (assetRoot.empty())
  {
    const auto found = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
    if (!found)
    {
      std::fputs("AssetGenerator: cannot find Assets/Scene.txt above the executable; pass --out\n", stderr);
      return 3;
    }
    assetRoot = *found;
  }

  const Neuron::VoxPalette& palette = Outpost::CanonicalPalette();
  int failures = 0;
  for (const AssetGen::ModelRecipe& recipe : AssetGen::Recipes())
  {
    const Neuron::VoxModel model = recipe.build();
    const std::vector<std::uint8_t> bytes = Neuron::WriteVox(model, palette);
    const std::filesystem::path path = assetRoot / L"Models" / (std::string(recipe.name) + ".vox");
    if (check)
    {
      std::vector<std::uint8_t> onDisk;
      const bool same = Neuron::ReadFileBytes(path, onDisk) && onDisk == bytes;
      std::printf("%-16s %3u x %3u x %3u  %7zu voxels  %s\n", recipe.name, model.SizeX(), model.SizeY(), model.SizeZ(), model.CountSolid(),
                  same ? "up to date" : "STALE");
      failures += same ? 0 : 1;
      continue;
    }
    if (!Neuron::WriteFileBytes(path, bytes))
    {
      std::fprintf(stderr, "AssetGenerator: cannot write %s\n", path.string().c_str());
      ++failures;
      continue;
    }
    std::printf("%-16s %3u x %3u x %3u  %7zu voxels  -> %s\n", recipe.name, model.SizeX(), model.SizeY(), model.SizeZ(), model.CountSolid(),
                path.string().c_str());
  }
  return failures == 0 ? 0 : 1;
}
