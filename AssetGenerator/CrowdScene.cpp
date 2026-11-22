#include "CrowdScene.h"

#include "FileSys.h"
#include "ModelCatalog.h"
#include "Palette.h"
#include "Random.h"
#include "VoxFile.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace AssetGen
{

namespace
{

constexpr float PI = 3.14159265358979f;
constexpr std::uint64_t CROWD_SEED = 20261122;

// GameDesign.md §8.2's load: 100 players, each with a ship and a wing of three, and about 200
// raiders and other traffic; up to 20 outposts in view.
constexpr int LANCER_COUNT = 260;
constexpr int TALON_COUNT = 260;
constexpr int TUG_COUNT = 50;
constexpr int FRIGATE_COUNT = 30;
constexpr int OUTPOST_COUNT = 20;
constexpr float CROWD_RADIUS_METERS = 900.0f;
constexpr float OUTPOST_RING_INNER_METERS = 1500.0f;
constexpr float OUTPOST_RING_OUTER_METERS = 3500.0f;

// The stand-in for a level of detail: the factor whose voxels still project to about this many
// pixels at 1080 lines. Below 3 pixels a voxel is drawn at 2x, below 1.5 at 4x.
constexpr float LOD2_BELOW_PIXELS = 3.0f;
constexpr float LOD4_BELOW_PIXELS = 1.5f;
constexpr float SCREEN_HEIGHT_PIXELS = 1080.0f;
constexpr float FOV_DEGREES = 38.0f;

struct Float3
{
  float x;
  float y;
  float z;
};

struct Camera
{
  Float3 target;
  float yawDegrees;
  float pitchDegrees;
  float distanceMeters;

  // OrbitCamera::Position(): the eye sits behind the target along the view direction.
  [[nodiscard]] Float3 Eye() const noexcept
  {
    const float yaw = yawDegrees * PI / 180.0f;
    const float pitch = pitchDegrees * PI / 180.0f;
    const Float3 forward{std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    return Float3{target.x - forward.x * distanceMeters, target.y - forward.y * distanceMeters, target.z - forward.z * distanceMeters};
  }
};

struct Placement
{
  std::string model;
  float voxelSize;
  Float3 position;
  Float3 rotationDegrees;
  float scale;
};

// One outpost: the sample shipyard's station modules and its claim asteroid, relative to the
// outpost's origin (Assets/Scene.txt).
struct OutpostPart
{
  const char* model;
  Float3 position;
  Float3 rotationDegrees;
};

constexpr std::array<OutpostPart, 13> OUTPOST_PARTS{{
  {"HangarBay", {0, 40, 120}, {0, 0, 0}},
  {"HangarSection", {-58, 36, 118}, {0, 0, 0}},
  {"CommandBlock", {-14, 82, 136}, {0, 0, 0}},
  {"TankCluster", {-98, 36, 114}, {0, 0, 0}},
  {"TrussBeam", {-158, 40, 118}, {0, 0, 0}},
  {"TrussBeam", {-150, 10, 104}, {0, 0, 0}},
  {"TrussBeam", {12, 64, 150}, {0, 0, 0}},
  {"TrussTower", {-74, 74, 118}, {0, 0, 0}},
  {"TrussTower", {24, 82, 132}, {0, 0, 0}},
  {"TrussTower", {-26, -2, 124}, {0, 180, 0}},
  {"DockingCollar", {-128, 10, 92}, {0, 0, 0}},
  {"DockingCollar", {62, 64, 140}, {90, 0, 0}},
  {"AsteroidMajor", {-300, 20, 170}, {20, 0, 0}},
}};

[[nodiscard]] float VoxelSizeOf(std::string_view _model) noexcept
{
  return _model.starts_with("Asteroid") ? 2.0f : 1.0f;
}

[[nodiscard]] std::vector<Placement> PlaceCrowd()
{
  Neuron::Pcg32 random(CROWD_SEED);
  std::vector<Placement> placements;
  const auto addShips = [&](const char* _model, int _count, float _scale)
  {
    for (int i = 0; i < _count; ++i)
    {
      Float3 position{};
      do
      {
        position = Float3{random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f)};
      } while (position.x * position.x + position.y * position.y + position.z * position.z > 1.0f);
      position = Float3{position.x * CROWD_RADIUS_METERS, position.y * CROWD_RADIUS_METERS * 0.5f, position.z * CROWD_RADIUS_METERS};
      const Float3 rotation{random.Range(-180.0f, 180.0f), random.Range(-30.0f, 30.0f), random.Range(-45.0f, 45.0f)};
      placements.push_back(Placement{_model, VoxelSizeOf(_model), position, rotation, _scale});
    }
  };
  addShips("EscortLancer", LANCER_COUNT, 1.0f);
  addShips("EscortTalon", TALON_COUNT, 1.0f);
  addShips("Tug", TUG_COUNT, 1.0f);
  addShips("Frigate", FRIGATE_COUNT, 1.25f);

  for (int i = 0; i < OUTPOST_COUNT; ++i)
  {
    const float angle = (static_cast<float>(i) + random.Range(-0.3f, 0.3f)) * 2.0f * PI / static_cast<float>(OUTPOST_COUNT);
    const float radius = random.Range(OUTPOST_RING_INNER_METERS, OUTPOST_RING_OUTER_METERS);
    const Float3 origin{std::sin(angle) * radius, random.Range(-250.0f, 250.0f), std::cos(angle) * radius};
    for (const OutpostPart& part : OUTPOST_PARTS)
    {
      placements.push_back(Placement{part.model, VoxelSizeOf(part.model),
                                     Float3{origin.x + part.position.x, origin.y + part.position.y, origin.z + part.position.z},
                                     part.rotationDegrees, 1.0f});
    }
  }
  return placements;
}

/// 1, 2 or 4: the stand-in a placement is drawn with, from how large its voxels project.
[[nodiscard]] int LodFactor(const Placement& _placement, const Float3& _eye) noexcept
{
  const float dx = _placement.position.x - _eye.x;
  const float dy = _placement.position.y - _eye.y;
  const float dz = _placement.position.z - _eye.z;
  const float distance = std::max(1.0f, std::sqrt(dx * dx + dy * dy + dz * dz));
  const float pixels =
    SCREEN_HEIGHT_PIXELS * _placement.voxelSize * _placement.scale / (2.0f * distance * std::tan(FOV_DEGREES * 0.5f * PI / 180.0f));
  if (pixels < LOD4_BELOW_PIXELS)
  {
    return 4;
  }
  return pixels < LOD2_BELOW_PIXELS ? 2 : 1;
}

[[nodiscard]] std::string ModelName(const std::string& _model, int _factor)
{
  return _factor == 1 ? _model : _model + "Lod" + std::to_string(_factor);
}

[[nodiscard]] std::string SceneText(const std::vector<Placement>& _placements, const Camera& _camera, bool _withStandIns,
                                    std::set<std::string>& _outModels, int (&_outCounts)[3])
{
  std::string text = "# Generated by AssetGenerator --crowd for the crowd benchmark (plan task F6). Not shipped content.\n";
  char line[512]{};
  std::snprintf(line, sizeof(line), "camera target=%.1f,%.1f,%.1f yaw=%.1f pitch=%.1f distance=%.1f fov=%.1f\n", _camera.target.x,
                _camera.target.y, _camera.target.z, _camera.yawDegrees, _camera.pitchDegrees, _camera.distanceMeters, FOV_DEGREES);
  text += line;
  text += "keylight direction=0.35,0.80,-0.48 color=0.74,0.84,1.0 intensity=2.3 ambientsky=0.070,0.085,0.120 "
          "ambientground=0.030,0.027,0.028\n";
  text += "stars seed=20261121 count=900\n";

  const Float3 eye = _camera.Eye();
  std::string instances;
  std::map<std::string, float> models;
  _outCounts[0] = _outCounts[1] = _outCounts[2] = 0;
  for (std::size_t i = 0; i < _placements.size(); ++i)
  {
    const Placement& placement = _placements[i];
    const int factor = _withStandIns ? LodFactor(placement, eye) : 1;
    ++_outCounts[factor == 1 ? 0 : factor == 2 ? 1 : 2];
    const std::string model = ModelName(placement.model, factor);
    models[model] = placement.voxelSize * static_cast<float>(factor);
    std::snprintf(line, sizeof(line), "instance name=C%04zu model=%s position=%.1f,%.1f,%.1f rotation=%.1f,%.1f,%.1f scale=%.2f\n", i,
                  model.c_str(), placement.position.x, placement.position.y, placement.position.z, placement.rotationDegrees.x,
                  placement.rotationDegrees.y, placement.rotationDegrees.z, placement.scale);
    instances += line;
  }
  for (const auto& [name, voxelSize] : models)
  {
    std::snprintf(line, sizeof(line), "model name=%s file=Models/%s.vox voxelsize=%g\n", name.c_str(), name.c_str(),
                  static_cast<double>(voxelSize));
    text += line;
    _outModels.insert(name);
  }
  text += instances;
  text += "objective text=\"Crowd benchmark\"\n";
  text += "resource name=Alloy amount=1000\n";
  return text;
}

[[nodiscard]] bool WriteText(const std::filesystem::path& _path, const std::string& _text)
{
  return Neuron::WriteFileBytes(_path, std::vector<std::uint8_t>(_text.begin(), _text.end()));
}

} // namespace

Neuron::VoxModel Downsample(const Neuron::VoxModel& _model, int _factor)
{
  const auto coarse = [_factor](std::uint32_t _size)
  { return (_size + static_cast<std::uint32_t>(_factor) - 1) / static_cast<std::uint32_t>(_factor); };
  Neuron::VoxModel result(coarse(_model.SizeX()), coarse(_model.SizeY()), coarse(_model.SizeZ()));
  const int threshold = (_factor * _factor * _factor + 3) / 4;
  for (int z = 0; z < static_cast<int>(result.SizeZ()); ++z)
  {
    for (int y = 0; y < static_cast<int>(result.SizeY()); ++y)
    {
      for (int x = 0; x < static_cast<int>(result.SizeX()); ++x)
      {
        std::array<int, 256> counts{};
        int solid = 0;
        for (int dz = 0; dz < _factor; ++dz)
        {
          for (int dy = 0; dy < _factor; ++dy)
          {
            for (int dx = 0; dx < _factor; ++dx)
            {
              const std::uint8_t color = _model.At(x * _factor + dx, y * _factor + dy, z * _factor + dz);
              if (color != 0)
              {
                ++counts[color];
                ++solid;
              }
            }
          }
        }
        if (solid < threshold)
        {
          continue;
        }
        int best = 1;
        for (int color = 1; color < 256; ++color)
        {
          if (counts[static_cast<std::size_t>(color)] > counts[static_cast<std::size_t>(best)])
          {
            best = color;
          }
        }
        result.Set(x, y, z, static_cast<std::uint8_t>(best));
      }
    }
  }
  return result;
}

int WriteCrowdScenes(const std::filesystem::path& _outDir)
{
  const std::vector<Placement> placements = PlaceCrowd();
  const Camera battle{{0.0f, 0.0f, 0.0f}, 30.0f, 12.0f, 300.0f};
  const Camera overview{{0.0f, 0.0f, 0.0f}, 30.0f, 28.0f, 2400.0f};
  struct Variant
  {
    const char* name;
    const Camera* camera;
    bool standIns;
  };
  const std::array<Variant, 4> variants{{
    {"Battle", &battle, false},
    {"BattleLod", &battle, true},
    {"Overview", &overview, false},
    {"OverviewLod", &overview, true},
  }};

  std::map<std::string, Neuron::VoxModel> built;
  for (const ModelRecipe& recipe : Recipes())
  {
    built.emplace(recipe.name, recipe.build());
  }

  int failures = 0;
  const Neuron::VoxPalette& palette = Outpost::CanonicalPalette();
  for (const Variant& variant : variants)
  {
    std::set<std::string> models;
    int counts[3]{};
    const std::filesystem::path root = _outDir / variant.name;
    if (!WriteText(root / "Scene.txt", SceneText(placements, *variant.camera, variant.standIns, models, counts)))
    {
      ++failures;
      continue;
    }
    for (const std::string& name : models)
    {
      const std::size_t lod = name.find("Lod");
      const std::string base = lod == std::string::npos ? name : name.substr(0, lod);
      const int factor = lod == std::string::npos ? 1 : std::stoi(name.substr(lod + 3));
      const Neuron::VoxModel& source = built.at(base);
      const Neuron::VoxModel model = factor == 1 ? source : Downsample(source, factor);
      if (!Neuron::SaveVoxFile(root / "Models" / (name + ".vox"), model, palette))
      {
        ++failures;
      }
    }
    std::printf("%-12s %zu instances (%d full, %d at 2x, %d at 4x), %zu models -> %s\n", variant.name, placements.size(), counts[0],
                counts[1], counts[2], models.size(), root.string().c_str());
  }
  return failures;
}

} // namespace AssetGen
