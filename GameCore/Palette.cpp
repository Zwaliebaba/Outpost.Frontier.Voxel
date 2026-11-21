#include "Palette.h"

#include <algorithm>
#include <cmath>

namespace Outpost
{

namespace
{

struct Color3
{
  float r;
  float g;
  float b;
};

[[nodiscard]] Neuron::Rgba8 ToRgba(const Color3& _c) noexcept
{
  const auto channel = [](float _v) noexcept { return static_cast<std::uint8_t>(std::lround(std::clamp(_v, 0.0f, 255.0f))); };
  return Neuron::Rgba8{channel(_c.r), channel(_c.g), channel(_c.b), 255};
}

// Fills _count entries starting at _first with an even ramp from _dark to _light.
void Ramp(Neuron::VoxPalette& _palette, int _first, int _count, const Color3& _dark, const Color3& _light) noexcept
{
  for (int i = 0; i < _count; ++i)
  {
    const float t = _count > 1 ? static_cast<float>(i) / static_cast<float>(_count - 1) : 0.0f;
    _palette[static_cast<std::size_t>(_first) + static_cast<std::size_t>(i)] =
      ToRgba(Color3{_dark.r + (_light.r - _dark.r) * t, _dark.g + (_light.g - _dark.g) * t, _dark.b + (_light.b - _dark.b) * t});
  }
}

[[nodiscard]] Neuron::VoxPalette BuildPalette() noexcept
{
  using namespace PaletteIndex;
  Neuron::VoxPalette palette{};
  palette[0] = Neuron::Rgba8{0, 0, 0, 0};

  Ramp(palette, HULL_CHARCOAL, 8, {34, 36, 40}, {70, 73, 79});
  Ramp(palette, ARMOR_GRAY, 8, {84, 87, 93}, {124, 127, 133});
  Ramp(palette, ARMOR_WHITE, 8, {170, 172, 172}, {226, 225, 219});
  Ramp(palette, SAFETY_ORANGE, 8, {168, 64, 18}, {242, 124, 44});
  Ramp(palette, HAZARD_YELLOW, 4, {196, 150, 34}, {232, 190, 58});
  Ramp(palette, HAZARD_DARK, 4, {24, 24, 26}, {44, 44, 46});
  Ramp(palette, ROCK, 8, {58, 48, 44}, {112, 94, 82});
  Ramp(palette, ROCK + 8, 8, {120, 101, 88}, {176, 152, 132});
  Ramp(palette, STEEL, 8, {72, 76, 82}, {150, 152, 156});
  Ramp(palette, DECK, 8, {66, 61, 56}, {124, 112, 98});
  Ramp(palette, GLASS, 4, {22, 32, 44}, {44, 62, 80});
  Ramp(palette, SCORCH, 4, {52, 36, 30}, {118, 76, 54});
  Ramp(palette, FRAME_DARK, 8, {24, 24, 27}, {52, 52, 56});
  Ramp(palette, INSIGNIA, 8, {22, 58, 70}, {60, 128, 146});

  // 97..199 are not assigned a role yet. They are filled with a neutral-to-cool ramp so that the
  // palette stays coherent when a .vox is opened in MagicaVoxel and someone paints with them.
  Ramp(palette, 97, 52, {18, 19, 22}, {236, 238, 240});
  Ramp(palette, 149, 51, {26, 34, 46}, {150, 176, 196});

  Ramp(palette, LAMP_AMBER, 1, {255, 184, 84}, {255, 184, 84});
  Ramp(palette, WINDOW_AMBER, 3, {236, 150, 60}, {255, 198, 112});
  Ramp(palette, FLOOD_WARM, 4, {244, 206, 150}, {255, 236, 196});
  Ramp(palette, ENGINE_CORE, 4, {60, 150, 255}, {170, 230, 255});
  Ramp(palette, BEACON_RED, 2, {230, 40, 30}, {255, 92, 70});
  Ramp(palette, NAV_GREEN, 2, {40, 220, 100}, {120, 255, 160});
  Ramp(palette, BREACH_EMBER, 4, {170, 40, 14}, {236, 104, 34});
  Ramp(palette, SIGNAL_CYAN, 4, {40, 200, 230}, {150, 245, 255});

  // 224..255: a deep charcoal-to-slate ramp, likewise unassigned.
  Ramp(palette, 224, 32, {12, 13, 16}, {96, 104, 116});
  return palette;
}

[[nodiscard]] Neuron::VoxelMaterialTable BuildMaterialTable() noexcept
{
  using namespace PaletteIndex;
  Neuron::VoxelMaterialTable table{};
  const auto set = [&](int _first, int _count, float _r, float _g, float _b, float _glow) noexcept
  {
    for (int i = 0; i < _count; ++i)
    {
      table[static_cast<std::size_t>(_first) + static_cast<std::size_t>(i)] = Neuron::VoxelMaterial{_r, _g, _b, _glow};
    }
  };
  set(LAMP_AMBER, 1, 6.0f, 3.4f, 1.1f, 2.6f);
  set(WINDOW_AMBER, 3, 3.2f, 1.7f, 0.5f, 0.0f);
  set(FLOOD_WARM, 4, 3.6f, 2.8f, 1.6f, 0.0f);
  set(ENGINE_CORE, 4, 1.4f, 4.2f, 9.0f, 0.0f);
  set(BEACON_RED, 2, 7.0f, 0.9f, 0.6f, 2.2f);
  set(NAV_GREEN, 2, 0.8f, 6.0f, 2.0f, 1.8f);
  set(BREACH_EMBER, 4, 1.5f, 0.28f, 0.05f, 0.0f);
  set(SIGNAL_CYAN, 4, 0.6f, 3.2f, 4.2f, 0.0f);
  return table;
}

} // namespace

const Neuron::VoxPalette& CanonicalPalette() noexcept
{
  static const Neuron::VoxPalette PALETTE = BuildPalette();
  return PALETTE;
}

const Neuron::VoxelMaterialTable& MaterialTable() noexcept
{
  static const Neuron::VoxelMaterialTable TABLE = BuildMaterialTable();
  return TABLE;
}

} // namespace Outpost
