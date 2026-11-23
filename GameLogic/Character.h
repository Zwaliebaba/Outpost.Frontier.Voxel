#pragma once

#include "FlightModel.h"
#include "SaveFile.h"
#include "UniverseSnapshot.h"
#include "VoxelMask.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Outpost
{

// The character record (plan task 2.6, ADR-011): what belongs to a player rather than to the universe
// or the process (AGENTS.md R23). It is what a save keeps of the player, and a second player is a
// second record. Content is named by its text identifiers, never by an index (R24).

enum class LocationKind : std::uint8_t
{
  Docked, // in a berth of a station (GameDesign.md D16)
  Space   // flying, with the ship's whole flight state
};

struct CharacterLocation
{
  LocationKind kind = LocationKind::Docked;
  std::string station;  // docked: the station (System.txt)
  std::string instance; // docked: the scene instance that carries the docking point
  std::string point;    // docked: the docking point on that instance's model
  FlightState flight;   // in space

  friend bool operator==(const CharacterLocation&, const CharacterLocation&) = default;
};

/// A ship as the character holds it: which one, and its condition and fit.
struct CharacterShip
{
  std::string instance; // the scene instance that is the ship
  int hullPercent = 100;
  std::vector<ShipModuleState> modules;
  std::vector<Neuron::CellSpan> absent; // the cells its mask lacks against its archetype, the intact master (ADR-012)

  friend bool operator==(const CharacterShip&, const CharacterShip&) = default;
};

struct CargoItem
{
  std::string item;
  std::int64_t count = 0;

  friend bool operator==(const CargoItem&, const CargoItem&) = default;
};

struct Character
{
  CharacterLocation location;
  CharacterShip ship;
  std::vector<CargoItem> cargo;
  std::int64_t money = 0;
  CharacterShip baseSnapshot;  // the ship as it was when last docked
  CharacterLocation base;      // where it was last docked: the base a death returns it to (GameDesign.md D1)
  std::int64_t repairKits = 0; // carried, for field repairs (plan task 3.11)
  // Interface state, as the client's own key and value pairs: the server keeps them and does not read
  // them.
  std::vector<std::pair<std::string, std::string>> interfaceState;

  friend bool operator==(const Character&, const Character&) = default;
};

inline constexpr std::string_view CHARACTER_SECTION = "Character";

/// Appends the character's section.
void WriteCharacter(const Character& _character, Neuron::SaveWriter& _writer);

/// Reads the character's section. Unknown records and keys, missing keys and values that do not
/// read are refused, with the line.
[[nodiscard]] bool ReadCharacter(const Neuron::SaveSection& _section, Character& _outCharacter, std::string& _outError);

} // namespace Outpost
