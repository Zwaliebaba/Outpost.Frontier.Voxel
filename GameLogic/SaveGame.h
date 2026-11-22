#pragma once

#include "Character.h"
#include "SaveFile.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

// A saved game (ADR-011): the character's section and the world's, each written by the part that
// owns it through a Neuron::SaveWriter, and any section this version does not read, kept as it was
// found and written back unchanged.

struct ResourceSave
{
  std::string name;
  std::int64_t amount = 0;
  std::int64_t remainder = 0; // income owed but not yet whole (World's ledger)

  friend bool operator==(const ResourceSave&, const ResourceSave&) = default;
};

struct ObjectiveSave
{
  std::string text; // objectives have no other name
  bool complete = false;

  friend bool operator==(const ObjectiveSave&, const ObjectiveSave&) = default;
};

/// The world as a save keeps it: its clock, the outpost's ledger and the objectives. The rest of the
/// world is content, rebuilt on load.
struct WorldSave
{
  std::uint64_t tick = 0;
  std::vector<ResourceSave> resources;
  std::vector<ObjectiveSave> objectives;

  friend bool operator==(const WorldSave&, const WorldSave&) = default;
};

struct SaveGame
{
  Character character;
  WorldSave world;
  std::vector<Neuron::SaveSection> kept; // sections this version does not read

  friend bool operator==(const SaveGame& _a, const SaveGame& _b) noexcept
  {
    return _a.character == _b.character && _a.world == _b.world;
  }
};

inline constexpr std::string_view WORLD_SECTION = "World";

void WriteWorld(const WorldSave& _world, Neuron::SaveWriter& _writer);
[[nodiscard]] bool ReadWorld(const Neuron::SaveSection& _section, WorldSave& _outWorld, std::string& _outError);

/// The save as the text of a save file: the character, the world, then the kept sections.
[[nodiscard]] bool FormatSaveGame(const SaveGame& _save, std::string& _outText, std::string& _outError);

/// A save file's text. Both sections are required; any other section is kept. Refused with the
/// reason, as ParseSave, ReadCharacter and ReadWorld give it.
[[nodiscard]] bool ParseSaveGame(std::string_view _text, SaveGame& _outSave, std::string& _outError);

} // namespace Outpost
