// Shipyard modules. The station is assembled from these with scene transforms (Assets/Scene.txt);
// HangarSection is used both solid and as the blueprint extension, from the same geometry.

#include "ModelCatalog.h"

#include "Palette.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace AssetGen
{

namespace
{

namespace Pal = Outpost::PaletteIndex;

[[nodiscard]] constexpr std::uint8_t Tone(std::uint8_t _family, int _shade) noexcept
{
  return static_cast<std::uint8_t>(_family + _shade);
}

} // namespace

Neuron::VoxModel BuildHangarBay()
{
  // The warm-lit open hangar: 64 x 52 x 38. The mouth faces -Y, toward the default camera.
  VoxelBuilder b(64, 52, 38, 2001);

  // Floor slab with an apron in front of the mouth.
  b.Box(Int3{0, 0, 0}, Int3{63, 51, 3}, PLACEHOLDER);
  // Walls, back and roof.
  b.Box(Int3{0, 10, 4}, Int3{4, 51, 33}, PLACEHOLDER);
  b.Box(Int3{59, 10, 4}, Int3{63, 51, 33}, PLACEHOLDER);
  b.Box(Int3{0, 47, 4}, Int3{63, 51, 33}, PLACEHOLDER);
  b.Box(Int3{0, 10, 30}, Int3{63, 51, 33}, PLACEHOLDER);
  // The mouth: pillars and a lintel framing the opening.
  b.Box(Int3{0, 8, 0}, Int3{6, 13, 33}, PLACEHOLDER);
  b.Box(Int3{57, 8, 0}, Int3{63, 13, 33}, PLACEHOLDER);
  b.Box(Int3{0, 8, 26}, Int3{63, 13, 33}, PLACEHOLDER);
  // Roof gear.
  b.Box(Int3{8, 18, 34}, Int3{15, 25, 35}, PLACEHOLDER);
  b.Box(Int3{44, 28, 34}, Int3{53, 39, 37}, PLACEHOLDER);
  b.Box(Int3{20, 40, 34}, Int3{38, 44, 34}, PLACEHOLDER);

  PaintHull(b, Int3{0, 0, 0}, Int3{63, 51, 37}, PaintMix{44, 42, 6, 8}, Int3{6, 6, 5}, 61);

  // Interior: decking, ribbed back wall, light strips, ceiling beams and lamps.
  b.PaintPanels(Int3{5, 14, 3}, Int3{58, 46, 3}, Shade{Pal::DECK, 1, 5}, 4);
  for (int x = 8; x <= 56; x += 6)
  {
    b.Box(Int3{x, 44, 4}, Int3{x + 1, 46, 29}, Tone(Pal::FRAME_DARK, 3));
  }
  b.PaintPanels(Int3{5, 47, 4}, Int3{58, 47, 29}, Shade{Pal::FRAME_DARK, 2, 6}, 3);
  b.PaintPanels(Int3{5, 14, 29}, Int3{58, 46, 29}, Shade{Pal::FRAME_DARK, 1, 4}, 4);
  b.PaintPanels(Int3{4, 14, 4}, Int3{4, 46, 29}, Shade{Pal::FRAME_DARK, 2, 6}, 3);
  b.PaintPanels(Int3{59, 14, 4}, Int3{59, 46, 29}, Shade{Pal::FRAME_DARK, 2, 6}, 3);
  for (const int z : {13, 22})
  {
    b.Box(Int3{5, 46, z}, Int3{58, 46, z}, Tone(Pal::WINDOW_AMBER, 1));
  }
  for (int y = 16; y <= 44; y += 7)
  {
    b.Box(Int3{5, y, 27}, Int3{58, y + 1, 28}, Tone(Pal::STEEL, 3));
  }
  for (const int x : {12, 24, 39, 51})
  {
    for (const int y : {20, 34})
    {
      b.Box(Int3{x, y, 26}, Int3{x + 1, y + 1, 26}, Pal::LAMP_AMBER);
    }
  }
  for (const int x : {18, 19, 44, 45})
  {
    b.Box(Int3{x, 14, 3}, Int3{x, 44, 3}, Tone(Pal::FLOOD_WARM, 1));
  }
  // Mezzanine decks along both walls, with railings.
  for (const int x0 : {5, 51})
  {
    b.Box(Int3{x0, 14, 14}, Int3{x0 + 7, 44, 15}, Tone(Pal::DECK, 3));
    const int rail = x0 == 5 ? x0 + 7 : x0;
    for (int y = 14; y <= 44; y += 2)
    {
      b.Set(rail, y, 16, Tone(Pal::STEEL, 5));
    }
    b.Box(Int3{rail, 14, 17}, Int3{rail, 44, 17}, Tone(Pal::STEEL, 5));
    b.Box(Int3{x0, 14, 13}, Int3{x0 + 7, 44, 13}, Tone(Pal::FLOOD_WARM, 0));
  }
  // Cargo on the floor.
  b.Box(Int3{24, 30, 4}, Int3{28, 34, 7}, Tone(Pal::SAFETY_ORANGE, 5));
  b.Box(Int3{30, 36, 4}, Int3{33, 40, 6}, Tone(Pal::ARMOR_GRAY, 4));
  b.Box(Int3{40, 24, 4}, Int3{43, 27, 8}, Tone(Pal::ARMOR_WHITE, 5));
  b.Box(Int3{14, 26, 4}, Int3{17, 29, 6}, Tone(Pal::SAFETY_ORANGE, 3));

  // The mouth's trim: hazard stripes on the apron edge and pillar faces, lamps under the lintel.
  b.HazardStripes(Int3{0, 0, 3}, Int3{63, 2, 3}, 2);
  b.HazardStripes(Int3{0, 8, 0}, Int3{6, 8, 25}, 3);
  b.HazardStripes(Int3{57, 8, 0}, Int3{63, 8, 25}, 3);
  b.Paint(Int3{7, 8, 26}, Int3{56, 8, 27}, Tone(Pal::SAFETY_ORANGE, 5));
  for (int x = 9; x <= 55; x += 5)
  {
    b.Set(x, 12, 25, Pal::LAMP_AMBER);
  }
  // Exterior windows along the right wall and the roof tower.
  for (int y = 16; y <= 44; y += 3)
  {
    b.Set(63, y, 21, Tone(Pal::WINDOW_AMBER, 0));
    b.Set(0, y, 21, Tone(Pal::WINDOW_AMBER, 0));
  }
  for (int x = 45; x <= 52; x += 2)
  {
    b.Set(x, 28, 36, Tone(Pal::WINDOW_AMBER, 2));
  }
  b.Set(48, 33, 37, Pal::BEACON_RED);
  return b.Model();
}

Neuron::VoxModel BuildHangarSection()
{
  // A ribbed hangar section: 48 x 36 x 30. Solid, it is the "F-02" block beside the hangar;
  // as a blueprint, the planned extension on the hangar's docking socket.
  VoxelBuilder b(48, 36, 30, 2002);
  b.Box(Int3{2, 2, 2}, Int3{45, 33, 27}, PLACEHOLDER);
  for (const int x : {2, 11, 20, 29, 38, 44})
  {
    b.Box(Int3{x, 1, 1}, Int3{x + 1, 34, 28}, Tone(Pal::ARMOR_GRAY, 3));
  }
  PaintHull(b, Int3{0, 0, 0}, Int3{47, 35, 29}, PaintMix{50, 42, 4, 4}, Int3{6, 6, 6}, 71);
  // Docking rings on both ends.
  b.CylinderX(18.0f, 15.0f, 7.0f, 0, 1, Tone(Pal::STEEL, 4));
  b.CylinderX(18.0f, 15.0f, 4.5f, 0, 1, Tone(Pal::FRAME_DARK, 1));
  b.CylinderX(18.0f, 15.0f, 7.0f, 46, 47, Tone(Pal::STEEL, 4));
  b.CylinderX(18.0f, 15.0f, 4.5f, 46, 47, Tone(Pal::FRAME_DARK, 1));
  // Registration, an orange band and a window strip on the front face.
  b.TextFrontY("F-02", 5, 0, 8, 2, Tone(Pal::ARMOR_WHITE, 6));
  b.Paint(Int3{2, 1, 4}, Int3{45, 1, 5}, Tone(Pal::SAFETY_ORANGE, 5));
  for (int x = 4; x <= 43; x += 3)
  {
    b.Set(x, 1, 24, Tone(Pal::WINDOW_AMBER, 1));
  }
  b.Set(23, 18, 29, Pal::LAMP_AMBER);
  return b.Model();
}

Neuron::VoxModel BuildCommandBlock()
{
  // A stepped command block with lit window rows and the station emblem: 48 x 40 x 46.
  VoxelBuilder b(48, 40, 46, 2003);
  b.Box(Int3{2, 4, 0}, Int3{45, 35, 19}, PLACEHOLDER);
  b.Box(Int3{8, 8, 20}, Int3{39, 31, 31}, PLACEHOLDER);
  b.Box(Int3{14, 12, 32}, Int3{33, 27, 39}, PLACEHOLDER);
  b.Box(Int3{4, 8, 20}, Int3{7, 14, 22}, PLACEHOLDER);
  b.Box(Int3{40, 24, 20}, Int3{43, 30, 23}, PLACEHOLDER);
  b.Box(Int3{36, 14, 32}, Int3{39, 19, 34}, PLACEHOLDER);
  PaintHull(b, Int3{0, 0, 0}, Int3{47, 39, 45}, PaintMix{40, 48, 6, 6}, Int3{5, 6, 5}, 81);

  for (int x = 6; x <= 42; x += 2)
  {
    b.Set(x, 4, 15, Tone(Pal::WINDOW_AMBER, 1));
    b.Set(x, 4, 10, Tone(Pal::WINDOW_AMBER, 0));
  }
  for (int x = 11; x <= 36; x += 2)
  {
    if (x < 19 || x > 29)
    {
      b.Set(x, 8, 27, Tone(Pal::WINDOW_AMBER, 2));
    }
  }
  for (int x = 16; x <= 31; x += 2)
  {
    b.Set(x, 12, 36, Tone(Pal::WINDOW_AMBER, 1));
  }
  b.Paint(Int3{2, 4, 18}, Int3{45, 4, 19}, Tone(Pal::SAFETY_ORANGE, 5));
  b.Paint(Int3{8, 8, 30}, Int3{39, 8, 31}, Tone(Pal::SAFETY_ORANGE, 4));
  b.HexEmblemY(24, 7, 25, 5, Tone(Pal::ARMOR_WHITE, 6), Tone(Pal::HULL_CHARCOAL, 1));
  // Antenna masts with beacons.
  for (const int x : {18, 30})
  {
    b.Box(Int3{x, 20, 40}, Int3{x, 20, 44}, Tone(Pal::STEEL, 6));
    b.Set(x, 20, 45, Pal::BEACON_RED);
  }
  b.Set(2, 4, 19, Pal::LAMP_AMBER);
  b.Set(45, 4, 19, Pal::LAMP_AMBER);
  return b.Model();
}

Neuron::VoxModel BuildTankCluster()
{
  // Two orange pressure tanks with white bands on a steel frame: 28 x 16 x 34.
  VoxelBuilder b(28, 16, 34, 2004);
  b.Box(Int3{1, 2, 0}, Int3{26, 13, 2}, Tone(Pal::STEEL, 3));
  for (const float cx : {8.0f, 20.0f})
  {
    b.CylinderZ(cx, 8.0f, 6.0f, 6, 26, Tone(Pal::SAFETY_ORANGE, 5));
    b.Ellipsoid(cx, 8.0f, 26.5f, 6.0f, 6.0f, 4.5f, Tone(Pal::SAFETY_ORANGE, 5));
    b.Ellipsoid(cx, 8.0f, 6.5f, 6.0f, 6.0f, 3.5f, Tone(Pal::SAFETY_ORANGE, 5));
  }
  // Shade the tanks by facing so they read as round, then band them.
  for (int z = 0; z < 34; ++z)
  {
    for (int y = 0; y < 16; ++y)
    {
      for (int x = 0; x < 28; ++x)
      {
        if (b.At(x, y, z) != Tone(Pal::SAFETY_ORANGE, 5))
        {
          continue;
        }
        const float cx = x < 14 ? 8.0f : 20.0f;
        const float angle = std::atan2(static_cast<float>(y) + 0.5f - 8.0f, static_cast<float>(x) + 0.5f - cx);
        const int shade = 3 + static_cast<int>((std::cos(angle - 2.4f) * 0.5f + 0.5f) * 4.99f);
        b.Set(x, y, z, Tone(Pal::SAFETY_ORANGE, shade));
      }
    }
  }
  for (const int z0 : {11, 21})
  {
    b.Paint(Int3{1, 1, z0}, Int3{26, 14, z0 + 2}, Tone(Pal::ARMOR_WHITE, 5));
  }
  b.Paint(Int3{1, 1, 3}, Int3{26, 14, 4}, Tone(Pal::ARMOR_WHITE, 3));
  b.CylinderX(8.0f, 18.0f, 1.2f, 8, 20, Tone(Pal::STEEL, 4));
  b.Box(Int3{13, 7, 3}, Int3{14, 8, 30}, Tone(Pal::STEEL, 2));
  b.Set(8, 1, 16, Pal::LAMP_AMBER);
  b.Set(20, 1, 16, Pal::LAMP_AMBER);
  return b.Model();
}

Neuron::VoxModel BuildDockingCollar()
{
  // A docking collar, a ring along Y: 18 x 12 x 18.
  VoxelBuilder b(18, 12, 18, 2005);
  b.CylinderY(9.0f, 9.0f, 8.6f, 0, 11, PLACEHOLDER);
  b.CylinderY(9.0f, 9.0f, 5.2f, 0, 11, Neuron::VOX_EMPTY);
  PaintHull(b, Int3{0, 0, 0}, Int3{17, 11, 17}, PaintMix{50, 47, 3, 0}, Int3{4, 4, 4}, 91);
  b.Paint(Int3{0, 4, 0}, Int3{17, 6, 17}, Tone(Pal::SAFETY_ORANGE, 5));
  b.CylinderY(9.0f, 9.0f, 5.9f, 0, 0, Tone(Pal::STEEL, 5));
  b.CylinderY(9.0f, 9.0f, 5.2f, 0, 0, Neuron::VOX_EMPTY);
  for (int i = 0; i < 8; ++i)
  {
    const float angle = static_cast<float>(i) * std::numbers::pi_v<float> / 4.0f;
    b.Set(static_cast<int>(9.0f + std::cos(angle) * 7.5f), 0, static_cast<int>(9.0f + std::sin(angle) * 7.5f), Tone(Pal::SIGNAL_CYAN, 2));
  }
  return b.Model();
}

Neuron::VoxModel BuildTrussBeam()
{
  // An exposed lattice girder, 96 long on X: 96 x 8 x 8.
  VoxelBuilder b(96, 8, 8, 2006);
  b.Truss(Int3{0, 0, 0}, Int3{95, 7, 7}, 0, 8, Tone(Pal::STEEL, 4), Tone(Pal::SAFETY_ORANGE, 4));
  for (int x = 12; x < 96; x += 24)
  {
    b.Set(x, 0, 0, Pal::LAMP_AMBER);
    b.Set(x, 0, 7, Tone(Pal::WINDOW_AMBER, 2));
  }
  return b.Model();
}

Neuron::VoxModel BuildTrussTower()
{
  // A lattice tower with a platform and a beacon: 12 x 12 x 48.
  VoxelBuilder b(12, 12, 48, 2007);
  b.Truss(Int3{0, 0, 0}, Int3{11, 11, 43}, 2, 6, Tone(Pal::FRAME_DARK, 5), Tone(Pal::STEEL, 3));
  b.Box(Int3{0, 0, 44}, Int3{11, 11, 45}, Tone(Pal::DECK, 3));
  b.HazardStripes(Int3{0, 0, 45}, Int3{11, 0, 45}, 2);
  b.Box(Int3{5, 5, 46}, Int3{6, 6, 46}, Tone(Pal::STEEL, 6));
  b.Box(Int3{5, 5, 47}, Int3{6, 6, 47}, Pal::BEACON_RED);
  for (int z = 6; z < 44; z += 12)
  {
    b.Set(0, 0, z, Pal::LAMP_AMBER);
    b.Set(11, 11, z, Pal::LAMP_AMBER);
  }
  return b.Model();
}

} // namespace AssetGen
