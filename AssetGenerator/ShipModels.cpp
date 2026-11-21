// Ships: the frigate with its pre-authored breach, two escorts with distinct silhouettes, the
// utility tug and the cargo module it carries. All face VOX +Y, with +Z up.

#include "ModelCatalog.h"

#include "Palette.h"

#include <algorithm>
#include <cmath>

namespace AssetGen
{

namespace
{

namespace Pal = Outpost::PaletteIndex;

[[nodiscard]] constexpr std::uint8_t Tone(std::uint8_t _family, int _shade) noexcept
{
  return static_cast<std::uint8_t>(_family + _shade);
}

// Removes the corner columns of a box along Y so a block reads as chamfered rather than square.
void ChamferAlongY(VoxelBuilder& _b, Int3 _min, Int3 _max) noexcept
{
  for (int y = _min.y; y <= _max.y; ++y)
  {
    _b.Set(_min.x, y, _min.z, Neuron::VOX_EMPTY);
    _b.Set(_max.x, y, _min.z, Neuron::VOX_EMPTY);
    _b.Set(_min.x, y, _max.z, Neuron::VOX_EMPTY);
    _b.Set(_max.x, y, _max.z, Neuron::VOX_EMPTY);
  }
}

// An engine nozzle on the -Y face of a block: a dark ring and a recessed glowing core.
void Nozzle(VoxelBuilder& _b, int _x0, int _x1, int _z0, int _z1, int _y) noexcept
{
  _b.Box(Int3{_x0, _y, _z0}, Int3{_x1, _y, _z1}, Tone(Pal::FRAME_DARK, 2));
  _b.Clear(Int3{_x0 + 1, _y, _z0 + 1}, Int3{_x1 - 1, _y, _z1 - 1});
  _b.Box(Int3{_x0 + 1, _y + 1, _z0 + 1}, Int3{_x1 - 1, _y + 1, _z1 - 1}, Tone(Pal::ENGINE_CORE, 2));
  _b.Box(Int3{_x0 + 2, _y + 1, _z0 + 2}, Int3{_x1 - 2, _y + 1, _z1 - 2}, Tone(Pal::ENGINE_CORE, 3));
}

// Frigate half-width (left half, so the hull spans 24 - w .. 23 before mirroring) along its length.
[[nodiscard]] int FrigateHalfWidth(int _y) noexcept
{
  if (_y < 30)
  {
    return 17;
  }
  if (_y < 104)
  {
    return 19;
  }
  return std::max(5, 19 - (_y - 104) * 13 / 36);
}

} // namespace

Neuron::VoxModel BuildFrigate()
{
  // 48 x 150 x 40, centred on x = 24. Built as the left half, mirrored, then the breach and the
  // starboard details are cut into the +X side, the side that faces the camera in the scene.
  VoxelBuilder b(48, 150, 40, 1001);
  constexpr int CX = 24;

  for (int y = 8; y <= 140; ++y)
  {
    const int w = FrigateHalfWidth(y);
    b.Box(Int3{CX - w + 2, y, 6}, Int3{CX - 1, y, 7}, PLACEHOLDER); // chamfered keel
    b.Box(Int3{CX - w, y, 8}, Int3{CX - 1, y, 12}, PLACEHOLDER);    // lower hull
    if (y >= 12 && y <= 134)
    {
      b.Box(Int3{CX - w + 2, y, 13}, Int3{CX - 1, y, 20}, PLACEHOLDER); // main deck, stepped in
    }
    if (y >= 22 && y <= 120)
    {
      b.Box(Int3{CX - w + 6, y, 21}, Int3{CX - 1, y, 24}, PLACEHOLDER); // upper armour
    }
    if (y >= 34 && y <= 112)
    {
      b.Box(Int3{CX - 9, y, 25}, Int3{CX - 1, y, 26}, PLACEHOLDER); // spine
    }
  }

  // Broad armoured sponsons on the flanks.
  b.Box(Int3{CX - 22, 36, 9}, Int3{CX - 19, 62, 18}, PLACEHOLDER);
  b.Box(Int3{CX - 22, 88, 9}, Int3{CX - 19, 104, 18}, PLACEHOLDER);
  b.Box(Int3{CX - 21, 40, 19}, Int3{CX - 17, 58, 20}, PLACEHOLDER);

  // Superstructure and bridge.
  b.Box(Int3{CX - 6, 66, 27}, Int3{CX - 1, 92, 32}, PLACEHOLDER);
  b.Box(Int3{CX - 4, 74, 33}, Int3{CX - 1, 88, 34}, PLACEHOLDER);
  b.Box(Int3{CX - 1, 80, 35}, Int3{CX - 1, 81, 38}, Tone(Pal::STEEL, 5));

  // Turrets with barrels.
  b.Box(Int3{CX - 4, 110, 25}, Int3{CX - 1, 117, 27}, PLACEHOLDER);
  b.Box(Int3{CX - 1, 118, 26}, Int3{CX - 1, 126, 26}, Tone(Pal::STEEL, 3));
  b.Box(Int3{CX - 4, 38, 27}, Int3{CX - 1, 45, 29}, PLACEHOLDER);
  b.Box(Int3{CX - 1, 46, 28}, Int3{CX - 1, 53, 28}, Tone(Pal::STEEL, 3));

  // Twin engine nacelles, proud of the stern.
  b.Box(Int3{8, 0, 7}, Int3{15, 14, 16}, PLACEHOLDER);
  ChamferAlongY(b, Int3{8, 0, 7}, Int3{15, 14, 16});

  PaintHull(b, Int3{0, 0, 0}, Int3{CX - 1, 149, 39}, PaintMix{46, 36, 6, 12}, Int3{5, 7, 4}, 11);

  // Painted details that must survive the mirror: orange nacelle bands, an orange bow, a dark
  // waterline, white deck edging.
  b.Paint(Int3{8, 10, 7}, Int3{15, 11, 16}, Tone(Pal::SAFETY_ORANGE, 5));
  b.Paint(Int3{0, 134, 6}, Int3{CX - 1, 140, 24}, Tone(Pal::SAFETY_ORANGE, 4));
  b.Paint(Int3{0, 8, 8}, Int3{CX - 1, 140, 8}, Tone(Pal::HULL_CHARCOAL, 1));
  Nozzle(b, 8, 15, 7, 16, 0);

  // Bridge windows: a lit band on the front of the superstructure and along its side.
  for (int x = CX - 5; x <= CX - 1; x += 2)
  {
    b.Set(x, 92, 31, Tone(Pal::WINDOW_AMBER, 1));
  }
  for (int y = 68; y <= 90; y += 3)
  {
    b.Set(CX - 6, y, 31, Tone(Pal::WINDOW_AMBER, 0));
  }
  b.Set(CX - 1, 81, 39, Pal::BEACON_RED);

  b.MirrorX();

  // ── The breach: pre-authored, on the starboard (+X) side ──────────────────────────────────
  constexpr float BREACH_Y = 72.0f;
  constexpr float BREACH_Z = 15.5f;
  const auto breachField = [&](int _x, int _y, int _z) noexcept
  {
    const float dy = (static_cast<float>(_y) - BREACH_Y) / 19.0f;
    const float dz = (static_cast<float>(_z) - BREACH_Z) / 9.5f;
    const float noise =
      Neuron::FractalNoise3(static_cast<float>(_x) * 0.35f, static_cast<float>(_y) * 0.35f, static_cast<float>(_z) * 0.35f, 3, 4242);
    return dy * dy + dz * dz + (noise - 0.5f) * 0.7f;
  };
  constexpr int BREACH_INNER_X = 31;
  for (int z = 6; z <= 26; ++z)
  {
    for (int y = 50; y <= 94; ++y)
    {
      for (int x = BREACH_INNER_X; x < 48; ++x)
      {
        if (breachField(x, y, z) < 1.0f)
        {
          b.Set(x, y, z, Neuron::VOX_EMPTY);
        }
      }
    }
  }
  // Back wall: dark plating with a warm-lit service strip.
  for (int z = 6; z <= 26; ++z)
  {
    for (int y = 50; y <= 94; ++y)
    {
      if (breachField(BREACH_INNER_X, y, z) < 1.0f && b.Solid(BREACH_INNER_X - 1, y, z))
      {
        b.Set(BREACH_INNER_X - 1, y, z, (y / 3 + z / 3) % 2 == 0 ? Tone(Pal::FRAME_DARK, 3) : Tone(Pal::DECK, 2));
      }
    }
  }
  // Deck layers and bulkhead frames inside the opening, broken toward the rim.
  for (int z = 6; z <= 26; ++z)
  {
    for (int y = 50; y <= 94; ++y)
    {
      for (int x = BREACH_INNER_X; x <= 44; ++x)
      {
        const float field = breachField(x, y, z);
        if (field >= 0.92f)
        {
          continue;
        }
        const bool deck = z == 10 || z == 15 || z == 20;
        const bool frame = (y % 6 == 0) && (x == BREACH_INNER_X || x == BREACH_INNER_X + 5 || z % 5 == 0 || x >= 40);
        const bool broken = Neuron::HashToUnit(x, y, z, 99) < (field > 0.55f ? 0.55f : 0.12f);
        if (deck && !broken && x <= 41)
        {
          b.Set(x, y, z, Tone(Pal::DECK, 1 + (x + y) % 4));
        }
        else if (frame && !broken)
        {
          b.Set(x, y, z, Tone(Pal::FRAME_DARK, 1 + z % 3));
        }
      }
    }
  }
  // Warm interior lamps on the back wall and a hot glow along the deck edges.
  constexpr int LAMP_Y[] = {57, 69, 81};
  for (const int y : LAMP_Y)
  {
    for (const int z : {12, 17, 22})
    {
      if (breachField(BREACH_INNER_X, y, z) < 0.8f)
      {
        b.Set(BREACH_INNER_X - 1, y, z, Pal::LAMP_AMBER);
        b.Set(BREACH_INNER_X - 1, y + 1, z, Tone(Pal::WINDOW_AMBER, 2));
      }
    }
  }
  b.Box(Int3{BREACH_INNER_X, 60, 12}, Int3{BREACH_INNER_X, 84, 12}, Tone(Pal::STEEL, 2));
  // The rim: every hull voxel bordering the cut, scorched, some still glowing.
  for (int z = 5; z <= 27; ++z)
  {
    for (int y = 48; y <= 96; ++y)
    {
      for (int x = BREACH_INNER_X; x < 48; ++x)
      {
        if (!b.Solid(x, y, z))
        {
          continue;
        }
        const float field = breachField(x, y, z);
        if (field < 0.9f || field > 1.35f)
        {
          continue;
        }
        const float h = Neuron::HashToUnit(x, y, z, 7);
        b.Set(x, y, z,
              h < 0.22f ? Tone(Pal::BREACH_EMBER, static_cast<int>(h * 16.0f) % 4) : Tone(Pal::SCORCH, static_cast<int>(h * 8.0f) % 4));
      }
    }
  }

  // Starboard insignia near the bow, and running lights: red to port, green to starboard.
  PaintHexOnSide(b, true, 118, 15, 4, Tone(Pal::ARMOR_WHITE, 6), Tone(Pal::HULL_CHARCOAL, 1));
  b.Set(CX + FrigateHalfWidth(126) - 1, 126, 12, Pal::NAV_GREEN);
  b.Set(CX - FrigateHalfWidth(126), 126, 12, Pal::BEACON_RED);
  return b.Model();
}

Neuron::VoxModel BuildEscortLancer()
{
  // A long dart with two engine pods on short wings: 28 x 52 x 18, centred on x = 14.
  VoxelBuilder b(28, 52, 18, 1002);
  constexpr int CX = 14;
  for (int y = 4; y <= 50; ++y)
  {
    const int half = y < 40 ? 4 : std::max(1, 4 - (y - 40) / 3);
    const int top = y < 42 ? 10 : std::max(6, 10 - (y - 42) / 2);
    b.Box(Int3{CX - half, y, 5}, Int3{CX - 1, y, top}, PLACEHOLDER);
  }
  b.Box(Int3{CX - 3, 12, 11}, Int3{CX - 1, 40, 12}, PLACEHOLDER);
  b.Box(Int3{CX - 2, 30, 13}, Int3{CX - 1, 38, 14}, Tone(Pal::GLASS, 2));
  b.Box(Int3{CX - 1, 8, 13}, Int3{CX - 1, 18, 15}, PLACEHOLDER); // dorsal fin
  // Wings and engine pods.
  b.Box(Int3{7, 14, 6}, Int3{CX - 4, 26, 7}, PLACEHOLDER);
  b.Box(Int3{3, 6, 3}, Int3{7, 30, 9}, PLACEHOLDER);
  ChamferAlongY(b, Int3{3, 6, 3}, Int3{7, 30, 9});
  b.Box(Int3{4, 31, 4}, Int3{6, 33, 8}, PLACEHOLDER);

  PaintHull(b, Int3{0, 0, 0}, Int3{CX - 1, 51, 17}, PaintMix{55, 37, 3, 5}, Int3{4, 5, 3}, 21);
  b.Paint(Int3{3, 24, 3}, Int3{7, 33, 9}, Tone(Pal::SAFETY_ORANGE, 5));
  b.Paint(Int3{7, 22, 6}, Int3{9, 26, 7}, Tone(Pal::SAFETY_ORANGE, 4));
  b.Paint(Int3{CX - 2, 14, 12}, Int3{CX - 1, 28, 12}, Tone(Pal::ARMOR_WHITE, 5));
  b.Set(CX - 2, 38, 13, Tone(Pal::WINDOW_AMBER, 1));
  b.Set(CX - 1, 38, 13, Tone(Pal::WINDOW_AMBER, 1));
  Nozzle(b, 3, 7, 3, 9, 6);
  b.MirrorX();
  b.Set(24, 30, 6, Pal::NAV_GREEN);
  b.Set(3, 30, 6, Pal::BEACON_RED);
  return b.Model();
}

Neuron::VoxModel BuildEscortTalon()
{
  // A flat body with forward-swept wings and one large engine: 44 x 42 x 16, centred on x = 22.
  VoxelBuilder b(44, 42, 16, 1003);
  constexpr int CX = 22;
  for (int y = 2; y <= 40; ++y)
  {
    const int half = y < 32 ? 4 : std::max(1, 4 - (y - 32) / 3);
    const int top = y < 34 ? 10 : std::max(6, 10 - (y - 34) / 2);
    b.Box(Int3{CX - half, y, 4}, Int3{CX - 1, y, top}, PLACEHOLDER);
  }
  b.Box(Int3{CX - 3, 14, 11}, Int3{CX - 1, 30, 12}, PLACEHOLDER);
  b.Box(Int3{CX - 2, 26, 13}, Int3{CX - 1, 31, 13}, Tone(Pal::GLASS, 3));
  // Forward-swept wing: the further out, the further forward.
  for (int t = 0; t <= 17; ++t)
  {
    const int x = CX - 5 - t;
    const int y0 = 10 + (t * 11) / 20;
    const int y1 = 19 + (t * 11) / 20 - t / 6;
    b.Box(Int3{x, y0, 6}, Int3{x, y1, 7}, PLACEHOLDER);
  }
  b.Box(Int3{0, 18, 5}, Int3{2, 30, 8}, PLACEHOLDER); // wingtip pod
  b.Box(Int3{CX - 3, 0, 5}, Int3{CX - 1, 6, 9}, PLACEHOLDER);

  PaintHull(b, Int3{0, 0, 0}, Int3{CX - 1, 41, 15}, PaintMix{50, 40, 4, 6}, Int3{4, 4, 3}, 31);
  b.Paint(Int3{0, 16, 4}, Int3{4, 31, 9}, Tone(Pal::SAFETY_ORANGE, 5));
  b.Paint(Int3{CX - 4, 33, 4}, Int3{CX - 3, 38, 10}, Tone(Pal::SAFETY_ORANGE, 3));
  b.Paint(Int3{CX - 1, 6, 11}, Int3{CX - 1, 30, 12}, Tone(Pal::ARMOR_WHITE, 6));
  b.Set(CX - 1, 31, 13, Tone(Pal::WINDOW_AMBER, 2));
  Nozzle(b, CX - 3, CX + 2, 5, 9, 0);
  b.MirrorX();
  Nozzle(b, CX - 3, CX + 2, 5, 9, 0);
  b.Set(43, 29, 6, Pal::NAV_GREEN);
  b.Set(0, 29, 6, Pal::BEACON_RED);
  return b.Model();
}

Neuron::VoxModel BuildTug()
{
  // A blunt utility tug with a raised cab and clamp arms: 26 x 38 x 22, centred on x = 13.
  VoxelBuilder b(26, 38, 22, 1004);
  constexpr int CX = 13;
  b.Box(Int3{CX - 8, 8, 3}, Int3{CX - 1, 30, 14}, PLACEHOLDER);
  ChamferAlongY(b, Int3{CX - 8, 8, 3}, Int3{CX - 1, 30, 14});
  b.Box(Int3{CX - 6, 24, 15}, Int3{CX - 1, 34, 18}, PLACEHOLDER);
  b.Box(Int3{CX - 6, 31, 10}, Int3{CX - 1, 34, 14}, PLACEHOLDER);
  b.Box(Int3{CX - 9, 0, 4}, Int3{CX - 1, 9, 13}, PLACEHOLDER);
  b.Box(Int3{2, 30, 5}, Int3{4, 37, 11}, PLACEHOLDER);
  b.Box(Int3{4, 35, 5}, Int3{6, 37, 11}, PLACEHOLDER);

  PaintHull(b, Int3{0, 0, 0}, Int3{CX - 1, 37, 21}, PaintMix{30, 49, 9, 12}, Int3{4, 4, 4}, 41);
  b.HazardStripes(Int3{2, 30, 5}, Int3{6, 37, 11}, 2);
  for (int x = CX - 5; x <= CX - 1; x += 2)
  {
    b.Set(x, 34, 16, Tone(Pal::WINDOW_AMBER, 1));
    b.Set(x, 34, 17, Tone(Pal::GLASS, 2));
  }
  b.Set(CX - 1, 30, 19, Pal::LAMP_AMBER);
  Nozzle(b, CX - 8, CX - 3, 6, 11, 0);
  b.MirrorX();
  return b.Model();
}

Neuron::VoxModel BuildCargoModule()
{
  // A prefab module in an orange carrying frame: 22 x 30 x 20.
  VoxelBuilder b(22, 30, 20, 1005);
  b.Box(Int3{2, 2, 2}, Int3{19, 27, 17}, PLACEHOLDER);
  PaintHull(b, Int3{0, 0, 0}, Int3{21, 29, 19}, PaintMix{20, 70, 10, 0}, Int3{5, 6, 5}, 51);
  const std::uint8_t frame = Tone(Pal::SAFETY_ORANGE, 6);
  for (const int y : {0, 28})
  {
    for (const int z : {0, 18})
    {
      b.Box(Int3{0, y, z}, Int3{21, y + 1, z + 1}, frame);
    }
  }
  for (const int x : {0, 20})
  {
    for (const int z : {0, 18})
    {
      b.Box(Int3{x, 0, z}, Int3{x + 1, 29, z + 1}, frame);
    }
    for (const int y : {0, 28})
    {
      b.Box(Int3{x, y, 0}, Int3{x + 1, y + 1, 19}, frame);
    }
  }
  b.Box(Int3{0, 14, 0}, Int3{1, 15, 19}, frame);
  b.Box(Int3{20, 14, 0}, Int3{21, 15, 19}, frame);
  b.HexEmblemX(20, 22, 10, 4, Tone(Pal::ARMOR_WHITE, 6), Tone(Pal::HULL_CHARCOAL, 1));
  b.HexEmblemX(1, 22, 10, 4, Tone(Pal::ARMOR_WHITE, 6), Tone(Pal::HULL_CHARCOAL, 1));
  b.Set(10, 29, 19, Pal::LAMP_AMBER);
  return b.Model();
}

} // namespace AssetGen
