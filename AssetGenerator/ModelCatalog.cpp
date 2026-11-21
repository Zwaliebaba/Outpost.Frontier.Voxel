#include "ModelCatalog.h"

#include "Palette.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace AssetGen
{

namespace
{

constexpr std::array<ModelRecipe, 19> RECIPES = {{
  {"Frigate", &BuildFrigate},
  {"EscortLancer", &BuildEscortLancer},
  {"EscortTalon", &BuildEscortTalon},
  {"Tug", &BuildTug},
  {"CargoModule", &BuildCargoModule},
  {"HangarBay", &BuildHangarBay},
  {"HangarSection", &BuildHangarSection},
  {"CommandBlock", &BuildCommandBlock},
  {"TankCluster", &BuildTankCluster},
  {"DockingCollar", &BuildDockingCollar},
  {"TrussBeam", &BuildTrussBeam},
  {"TrussTower", &BuildTrussTower},
  {"AsteroidMajor", &BuildAsteroidMajor},
  {"AsteroidSmallA", &BuildAsteroidSmallA},
  {"AsteroidSmallB", &BuildAsteroidSmallB},
  {"AsteroidSmallC", &BuildAsteroidSmallC},
  {"DebrisA", &BuildDebrisA},
  {"DebrisB", &BuildDebrisB},
  {"DebrisC", &BuildDebrisC},
}};

[[nodiscard]] float HexDistance(float _u, float _v) noexcept
{
  const float au = std::fabs(_u);
  const float av = std::fabs(_v);
  return std::max(au * 0.8660254f + av * 0.5f, av);
}

} // namespace

std::span<const ModelRecipe> Recipes() noexcept
{
  return RECIPES;
}

void PaintHull(VoxelBuilder& _builder, Int3 _min, Int3 _max, PaintMix _mix, Int3 _panel, std::uint32_t _salt) noexcept
{
  namespace Pal = Outpost::PaletteIndex;
  const std::uint32_t seed = _builder.Seed() * 2654435761u + _salt;
  for (int z = _min.z; z <= _max.z; ++z)
  {
    for (int y = _min.y; y <= _max.y; ++y)
    {
      for (int x = _min.x; x <= _max.x; ++x)
      {
        if (_builder.At(x, y, z) != PLACEHOLDER)
        {
          continue;
        }
        const int px = x / _panel.x;
        const int py = y / _panel.y;
        const int pz = z / _panel.z;
        // Orange is a sparse accent chosen per plate; the other families come from coherent noise
        // over the plates, so neighbouring plates tend to share a family and the hull reads as
        // painted sections rather than a checkerboard.
        const float shadePick = Neuron::HashToUnit(px, py, pz, seed ^ 0xA5A5A5A5u);
        const float accentPick = Neuron::HashToUnit(px, py, pz, seed ^ 0x5BD1E995u) * 100.0f;
        std::uint8_t color = 0;
        if (accentPick < static_cast<float>(_mix.orange))
        {
          color = static_cast<std::uint8_t>(Pal::SAFETY_ORANGE + 3 + static_cast<int>(shadePick * 4.0f));
        }
        else
        {
          const float noise =
            Neuron::ValueNoise3(static_cast<float>(px) * 0.55f, static_cast<float>(py) * 0.55f, static_cast<float>(pz) * 0.55f, seed);
          const float coherent =
            std::clamp((noise - 0.5f) * 1.9f + 0.5f, 0.0f, 1.0f) * 0.8f + Neuron::HashToUnit(px, py, pz, seed ^ 0x3C6EF372u) * 0.2f;
          const float total = static_cast<float>(_mix.charcoal + _mix.gray + _mix.white);
          const float pick = coherent * total;
          if (pick < static_cast<float>(_mix.charcoal))
          {
            color = static_cast<std::uint8_t>(Pal::HULL_CHARCOAL + 2 + static_cast<int>(shadePick * 4.0f));
          }
          else if (pick < static_cast<float>(_mix.charcoal + _mix.gray))
          {
            color = static_cast<std::uint8_t>(Pal::ARMOR_GRAY + 1 + static_cast<int>(shadePick * 4.0f));
          }
          else
          {
            color = static_cast<std::uint8_t>(Pal::ARMOR_WHITE + 1 + static_cast<int>(shadePick * 4.0f));
          }
        }
        _builder.Set(x, y, z, color);
      }
    }
  }
}

void PaintHexOnSide(VoxelBuilder& _builder, bool _positiveX, int _cy, int _cz, int _radius, std::uint8_t _plate,
                    std::uint8_t _mark) noexcept
{
  const auto r = static_cast<float>(_radius);
  for (int z = _cz - _radius - 2; z <= _cz + _radius + 2; ++z)
  {
    for (int y = _cy - _radius - 2; y <= _cy + _radius + 2; ++y)
    {
      const float d = HexDistance(static_cast<float>(y - _cy), static_cast<float>(z - _cz));
      if (d > r + 1.0f)
      {
        continue;
      }
      const bool mark = d > r - 1.2f || (d > r * 0.45f - 0.6f && d <= r * 0.45f + 0.6f) || (y == _cy && z <= _cz && d < r * 0.45f);
      for (int step = 0; step < _builder.SizeX(); ++step)
      {
        const int x = _positiveX ? _builder.SizeX() - 1 - step : step;
        if (_builder.Solid(x, y, z))
        {
          _builder.Set(x, y, z, mark ? _mark : _plate);
          break;
        }
      }
    }
  }
}

} // namespace AssetGen
