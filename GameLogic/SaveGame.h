#pragma once

#include "Character.h"
#include "SaveFile.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

// A saved game (ADR-011): the character's section and the universe's, each written by the part that
// owns it through a Neuron::SaveWriter, and any section this version does not read, kept as it was
// found and written back unchanged.

struct ResourceSave
{
  std::string name;
  std::int64_t amount = 0;
  std::int64_t remainder = 0; // income owed but not yet whole (Universe's ledger)

  friend bool operator==(const ResourceSave&, const ResourceSave&) = default;
};

struct ObjectiveSave
{
  std::string text; // objectives have no other name
  bool complete = false;

  friend bool operator==(const ObjectiveSave&, const ObjectiveSave&) = default;
};

/// An object's mask, as the cells it lacks against its archetype (ADR-012), by its instance name. Kept only
/// for an object whose mask is no longer the one it started with.
struct MaskSave
{
  std::string instance;
  std::vector<Neuron::CellSpan> absent;

  friend bool operator==(const MaskSave&, const MaskSave&) = default;
};

/// The universe as a save keeps it: its clock, the outpost's ledger and the objectives, the masks that
/// have changed and the authored objects that are gone, destroyed or fled (plan task 3.7). The rest of
/// the universe is content, rebuilt on load. Wrecks are not kept.
struct UniverseSave
{
  std::uint64_t tick = 0;
  std::vector<ResourceSave> resources;
  std::vector<ObjectiveSave> objectives;
  std::vector<MaskSave> masks;
  std::vector<std::string> gone;

  friend bool operator==(const UniverseSave&, const UniverseSave&) = default;
};

struct SaveGame
{
  Character character;
  UniverseSave universe;
  std::vector<Neuron::SaveSection> kept; // sections this version does not read

  friend bool operator==(const SaveGame& _a, const SaveGame& _b) noexcept
  {
    return _a.character == _b.character && _a.universe == _b.universe;
  }
};

inline constexpr std::string_view UNIVERSE_SECTION = "Universe";

void WriteUniverse(const UniverseSave& _universe, Neuron::SaveWriter& _writer);
[[nodiscard]] bool ReadUniverse(const Neuron::SaveSection& _section, UniverseSave& _outUniverse, std::string& _outError);

/// The save as the text of a save file: the character, the universe, then the kept sections.
[[nodiscard]] bool FormatSaveGame(const SaveGame& _save, std::string& _outText, std::string& _outError);

/// A save file's text. Both sections are required; any other section is kept. Refused with the
/// reason, as ParseSave, ReadCharacter and ReadUniverse give it.
[[nodiscard]] bool ParseSaveGame(std::string_view _text, SaveGame& _outSave, std::string& _outError);

} // namespace Outpost
