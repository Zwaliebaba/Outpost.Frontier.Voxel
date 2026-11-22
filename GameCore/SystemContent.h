#pragma once

#include "Zone.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

struct SceneDefinition;

// The system's content, Assets/Content/System.txt (plan tasks 2.1 and 2.2): its zones, its stations
// and where a new game starts. Both halves load it: the server for docking and hazards, the client
// for the hazard warning and the names it shows.

/// A station: a base the player can dock at, made of the scene instances whose docking points are
/// its own (blueprint ch. 04 §3.2). Its name is what a save records (AGENTS.md R24).
struct StationDefinition
{
  std::string name;
  std::string label;                  // what the interface calls it
  std::string region;                 // the named-region zone it lies in, or empty
  std::vector<std::string> instances; // scene instances, in the order given
};

struct SystemContent
{
  std::vector<Zone> zones;
  std::vector<StationDefinition> stations;
  std::string startStation; // where a new game starts, docked (GameDesign.md D18)

  [[nodiscard]] const Zone* FindZone(std::string_view _name) const noexcept;
  [[nodiscard]] const StationDefinition* FindStation(std::string_view _name) const noexcept;
  /// The station an instance belongs to, or null.
  [[nodiscard]] const StationDefinition* StationOf(std::string_view _instance) const noexcept;
};

/// Parses System.txt against the scene, which must have its model sizes set: a zone placed with
/// instance= has its position in that instance's VOX coordinates. Unknown keywords and keys, missing
/// keys, values out of range, duplicate names and dangling references are errors naming the file and
/// the line, and so is a station none of whose instances has a docking point.
[[nodiscard]] bool ParseSystemContent(std::string_view _text, std::string_view _fileName, const SceneDefinition& _scene,
                                      SystemContent& _outContent, std::string& _outError);
[[nodiscard]] bool LoadSystemContent(const std::filesystem::path& _path, const SceneDefinition& _scene, SystemContent& _outContent,
                                     std::string& _outError);

} // namespace Outpost
