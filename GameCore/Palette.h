#pragma once

#include "VoxModel.h"
#include "VoxelCarve.h"
#include "VoxelMaterial.h"

#include <cstdint>

namespace Outpost
{

// The canonical palette (ADR-002). Every committed .vox file embeds exactly this palette in its
// RGBA chunk, and an index means the same thing in every model. Each family is a run of shades so
// generators can vary panels without changing what a surface is. Indices are VOXEL colour indices
// (1..255); 0 is empty space.
namespace PaletteIndex
{
inline constexpr std::uint8_t HULL_CHARCOAL = 1;  // 1..8   charcoal hull plates, dark to light
inline constexpr std::uint8_t ARMOR_GRAY = 9;     // 9..16  gray armour
inline constexpr std::uint8_t ARMOR_WHITE = 17;   // 17..24 off-white armour
inline constexpr std::uint8_t SAFETY_ORANGE = 25; // 25..32 safety orange, dark to bright
inline constexpr std::uint8_t HAZARD_YELLOW = 33; // 33..36 hazard striping
inline constexpr std::uint8_t HAZARD_DARK = 37;   // 37..40 the dark half of a hazard stripe
inline constexpr std::uint8_t ROCK = 41;          // 41..56 muted asteroid rock, dark to light
inline constexpr std::uint8_t STEEL = 57;         // 57..64 truss steel
inline constexpr std::uint8_t DECK = 65;          // 65..72 interior decking, warm gray
inline constexpr std::uint8_t GLASS = 73;         // 73..76 unlit dark glass
inline constexpr std::uint8_t SCORCH = 77;        // 77..80 heat-scorched metal at a breach rim
inline constexpr std::uint8_t FRAME_DARK = 81;    // 81..88 recessed interior frames
inline constexpr std::uint8_t INSIGNIA = 89;      // 89..96 dark teal insignia paint

inline constexpr std::uint8_t ORE = 97; // 97..104 copper-green mineral seams

// Emissive families. Their meaning is fixed here and their emission is in MaterialTable().
inline constexpr std::uint8_t LAMP_AMBER = 200;    // a warm lamp; gets a glow sprite
inline constexpr std::uint8_t WINDOW_AMBER = 201;  // 201..203 lit windows; emission only
inline constexpr std::uint8_t FLOOD_WARM = 204;    // 204..207 hangar floods and floor strips
inline constexpr std::uint8_t ENGINE_CORE = 208;   // 208..211 cyan-blue engine cores
inline constexpr std::uint8_t BEACON_RED = 212;    // 212..213 red beacons; glow
inline constexpr std::uint8_t NAV_GREEN = 214;     // 214..215 green running lights; glow
inline constexpr std::uint8_t BREACH_EMBER = 216;  // 216..219 hot metal inside a hull breach
inline constexpr std::uint8_t SIGNAL_CYAN = 220;   // 220..223 small cyan status lights
inline constexpr std::uint8_t STROBE_YELLOW = 224; // bright yellow, double flash every 1.5 seconds
inline constexpr std::uint8_t WINDOW_BLUE = 225;   // blue glazing, 80/255 coverage

inline constexpr std::uint8_t FAMILY_SHADES = 8;
} // namespace PaletteIndex

/// The palette, indexed by voxel colour index. Entry 0 is transparent black.
[[nodiscard]] const Neuron::VoxPalette& CanonicalPalette() noexcept;

/// Each index's emission, glow radius and flash period. Shared by every rendered model.
[[nodiscard]] const Neuron::VoxelMaterialTable& MaterialTable() noexcept;

/// What a palette family is made of (plan task 3.2, ADR-014): what a cell costs a carve to remove,
/// in the same integer units a weapon's carve energy is given in; what it weighs; and what Ore
/// mining it yields (slice 5). Armour is a stronger outer material, not a divisor.
struct StructuralMaterial
{
  std::uint32_t strength = 0;
  float massKilograms = 0.0f;
  std::uint32_t oreYield = 0;
};

[[nodiscard]] const StructuralMaterial& MaterialOf(std::uint8_t _colorIndex) noexcept;
/// The strength of every colour index, as the carve reads it.
[[nodiscard]] const Neuron::StrengthTable& MaterialStrengths() noexcept;

/// What a player owns is this much tougher in its module cells (GameDesign.md §7.1: the blueprint
/// makes a player's parts three times harder to break than an NPC's in the same hull).
inline constexpr std::uint32_t PLAYER_MODULE_TOUGHNESS = 3;

} // namespace Outpost
