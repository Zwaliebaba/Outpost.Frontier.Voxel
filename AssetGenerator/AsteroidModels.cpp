// Rock and debris: the major asteroid the shipyard is anchored to, three smaller fragments, and
// the detached hull pieces that drift beside the frigate's breach.

#include "ModelCatalog.h"

#include "Palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace AssetGen
{

namespace
{

namespace Pal = Outpost::PaletteIndex;

[[nodiscard]] constexpr std::uint8_t Tone(std::uint8_t _family, int _shade) noexcept
{
  return static_cast<std::uint8_t>(_family + _shade);
}

struct Crater
{
  float x;
  float y;
  float z;
  float radius;
};

// A craggy rock: a noisy ellipsoid, roughened in blocks so its cliffs read as stepped voxels,
// with craters bitten out of it and coloured in muted bands.
void Rock(VoxelBuilder& _b, float _rx, float _ry, float _rz, float _roughness, int _blockSize, int _craterCount)
{
  const float cx = static_cast<float>(_b.SizeX()) * 0.5f;
  const float cy = static_cast<float>(_b.SizeY()) * 0.5f;
  const float cz = static_cast<float>(_b.SizeZ()) * 0.5f;
  const std::uint32_t seed = _b.Seed();
  const float scale = 2.4f / std::max({_rx, _ry, _rz});

  std::vector<Crater> craters;
  for (int i = 0; i < _craterCount; ++i)
  {
    const float theta = _b.Rng().Range(0.0f, 6.2831853f);
    const float phi = std::acos(_b.Rng().Range(-0.8f, 0.9f));
    const float radius = _b.Rng().Range(0.10f, 0.22f) * std::min({_rx, _ry, _rz});
    craters.push_back(Crater{cx + std::cos(theta) * std::sin(phi) * _rx * 0.98f, cy + std::sin(theta) * std::sin(phi) * _ry * 0.98f,
                             cz + std::cos(phi) * _rz * 0.98f, radius});
  }

  for (int z = 0; z < _b.SizeZ(); ++z)
  {
    for (int y = 0; y < _b.SizeY(); ++y)
    {
      for (int x = 0; x < _b.SizeX(); ++x)
      {
        const float px = static_cast<float>(x) + 0.5f - cx;
        const float py = static_cast<float>(y) + 0.5f - cy;
        const float pz = static_cast<float>(z) + 0.5f - cz;
        const float q = std::sqrt((px * px) / (_rx * _rx) + (py * py) / (_ry * _ry) + (pz * pz) / (_rz * _rz));
        if (q > 1.25f)
        {
          continue;
        }
        const float broad = Neuron::FractalNoise3(px * scale, py * scale, pz * scale, 4, seed);
        const float crag = Neuron::FractalNoise3(px * scale * 3.1f, py * scale * 3.1f, pz * scale * 3.1f, 3, seed + 17);
        const float block = Neuron::HashToUnit(x / _blockSize, y / _blockSize, z / _blockSize, seed + 31);
        const float threshold = 0.80f + (broad - 0.5f) * 0.55f * _roughness + (crag - 0.5f) * 0.22f * _roughness + (block - 0.5f) * 0.08f;
        if (q > threshold)
        {
          continue;
        }
        bool bitten = false;
        for (const Crater& crater : craters)
        {
          const float dx = static_cast<float>(x) + 0.5f - crater.x;
          const float dy = static_cast<float>(y) + 0.5f - crater.y;
          const float dz = static_cast<float>(z) + 0.5f - crater.z;
          if (dx * dx + dy * dy + dz * dz < crater.radius * crater.radius)
          {
            bitten = true;
            break;
          }
        }
        if (bitten)
        {
          continue;
        }
        // Colour: broad bands from the low-frequency noise, speckle from the high.
        const float tone = broad * 0.75f + crag * 0.35f + (static_cast<float>(z) / static_cast<float>(_b.SizeZ())) * 0.15f - 0.2f;
        const int shade = std::clamp(static_cast<int>(tone * 15.0f), 0, 15);
        _b.Set(x, y, z, Tone(Pal::ROCK, shade));
      }
    }
  }
}

// The outermost solid voxel seen from +X at (y, z), or -1.
[[nodiscard]] int SurfaceFromPositiveX(const VoxelBuilder& _b, int _y, int _z) noexcept
{
  for (int x = _b.SizeX() - 1; x >= 0; --x)
  {
    if (_b.Solid(x, _y, _z))
    {
      return x;
    }
  }
  return -1;
}

} // namespace

Neuron::VoxModel BuildAsteroidMajor()
{
  // 128 x 128 x 160 at two world units per voxel. Its +X face, toward the shipyard, carries the
  // mining lights and the small structures the trusses anchor into.
  VoxelBuilder b(128, 128, 160, 3001);
  Rock(b, 60.0f, 58.0f, 74.0f, 1.0f, 5, 9);

  // Lit anchor structures where the station's trusses meet the rock.
  constexpr std::array<std::array<int, 2>, 4> ANCHORS = {{{40, 92}, {70, 70}, {52, 108}, {88, 84}}};
  for (const auto& anchor : ANCHORS)
  {
    const int y = anchor[0];
    const int z = anchor[1];
    const int surface = SurfaceFromPositiveX(b, y, z);
    if (surface < 0)
    {
      continue;
    }
    b.Box(Int3{surface - 4, y - 3, z - 2}, Int3{surface + 2, y + 3, z + 2}, Tone(Pal::ARMOR_GRAY, 3));
    b.Box(Int3{surface - 4, y - 3, z + 3}, Int3{surface + 1, y + 3, z + 3}, Tone(Pal::HULL_CHARCOAL, 3));
    for (int dy = -2; dy <= 2; dy += 2)
    {
      b.Set(surface + 2, y + dy, z, Tone(Pal::WINDOW_AMBER, 1));
    }
    b.Set(surface + 2, y, z + 2, Pal::LAMP_AMBER);
  }
  // Scattered work lights on the station-facing side.
  Neuron::Pcg32 rng(77);
  for (int i = 0; i < 14; ++i)
  {
    const int y = rng.RangeInt(30, 110);
    const int z = rng.RangeInt(50, 130);
    const int surface = SurfaceFromPositiveX(b, y, z);
    if (surface >= 0)
    {
      b.Set(surface, y, z, i % 3 == 0 ? Pal::LAMP_AMBER : Tone(Pal::WINDOW_AMBER, 0));
    }
  }
  return b.Model();
}

Neuron::VoxModel BuildAsteroidSmallA()
{
  VoxelBuilder b(22, 20, 18, 3002);
  Rock(b, 9.5f, 8.5f, 7.5f, 1.2f, 3, 2);
  return b.Model();
}

Neuron::VoxModel BuildAsteroidSmallB()
{
  VoxelBuilder b(16, 18, 14, 3003);
  Rock(b, 6.5f, 7.8f, 5.6f, 1.3f, 2, 1);
  return b.Model();
}

Neuron::VoxModel BuildAsteroidSmallC()
{
  VoxelBuilder b(11, 10, 10, 3004);
  Rock(b, 4.8f, 4.2f, 4.2f, 1.4f, 2, 0);
  return b.Model();
}

Neuron::VoxModel BuildDebrisA()
{
  // A torn hull plate with a glowing edge.
  VoxelBuilder b(5, 4, 3, 3101);
  b.Box(Int3{0, 0, 1}, Int3{4, 3, 1}, Tone(Pal::HULL_CHARCOAL, 4));
  b.Box(Int3{1, 1, 0}, Int3{3, 2, 0}, Tone(Pal::FRAME_DARK, 3));
  b.Set(4, 3, 1, Tone(Pal::BREACH_EMBER, 2));
  b.Set(0, 0, 1, Neuron::VOX_EMPTY);
  b.Set(2, 2, 2, Tone(Pal::ARMOR_WHITE, 4));
  return b.Model();
}

Neuron::VoxModel BuildDebrisB()
{
  // A small chunk of armour.
  VoxelBuilder b(3, 3, 2, 3102);
  b.Box(Int3{0, 0, 0}, Int3{2, 1, 1}, Tone(Pal::ARMOR_GRAY, 4));
  b.Set(1, 2, 0, Tone(Pal::SCORCH, 2));
  b.Set(2, 1, 1, Tone(Pal::BREACH_EMBER, 1));
  return b.Model();
}

Neuron::VoxModel BuildDebrisC()
{
  // A bent frame member.
  VoxelBuilder b(4, 2, 3, 3103);
  b.Box(Int3{0, 0, 0}, Int3{3, 0, 0}, Tone(Pal::FRAME_DARK, 4));
  b.Box(Int3{3, 0, 1}, Int3{3, 1, 2}, Tone(Pal::FRAME_DARK, 4));
  b.Set(0, 1, 0, Tone(Pal::SAFETY_ORANGE, 4));
  return b.Model();
}

} // namespace AssetGen
