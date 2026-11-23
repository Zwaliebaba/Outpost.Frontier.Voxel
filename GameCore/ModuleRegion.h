#pragma once

#include "UniverseSnapshot.h"
#include "VoxelMask.h"
#include "VoxelMesher.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

// Hulls and their module regions (plan task 3.3, ADR-014). A hull is an intact master model, the
// archetype a damageable ship's mask is taken against (ADR-021); a module is a named box of its
// cells with a function. A module's health is the share of its cells present, and below its
// function's two thresholds it works at a reduced rate, then stops. Both halves load this content:
// the server to decide, the client to read the same conditions from the health the server states.

enum class ModuleFunction : std::uint8_t
{
  Core,       // its loss destroys the ship (GameDesign.md §7.1)
  Power,      // the reactor: the power pool's charge
  Shield,     // the shield generator: the shield's regeneration
  Engine,     // the engines: thrust
  Sensor,     // the scanner, from slice 3's contacts on
  Weapon,     // a weapon mount: its launcher
  Atmosphere, // life support, from slice 4's crew on
  Structure,  // a diagnostic section: counted, does nothing itself
  Clamp,      // a tug's clamps
  Cargo       // a tug's cargo systems
};

[[nodiscard]] std::optional<ModuleFunction> ParseModuleFunction(std::string_view _text) noexcept;
[[nodiscard]] std::string_view ModuleFunctionName(ModuleFunction _function) noexcept;

/// A named box of a hull's cells, VOX coordinates, minimum inclusive and maximum exclusive.
struct ModuleRegion
{
  std::string name;
  ModuleFunction function = ModuleFunction::Structure;
  Neuron::Int3 min{0, 0, 0};
  Neuron::Int3 max{0, 0, 0};

  [[nodiscard]] bool Contains(const Neuron::Int3& _cell) const noexcept
  {
    return _cell.x >= min.x && _cell.y >= min.y && _cell.z >= min.z && _cell.x < max.x && _cell.y < max.y && _cell.z < max.z;
  }
};

/// A hull of Assets/Hulls/Hulls.txt: the master the masks are taken against, the scene model that
/// draws it today, and its regions.
struct HullRecord
{
  std::string name;
  std::string master; // the intact master: Assets/Models/<master>.vox
  std::string legacy; // the scene model the ship is placed and drawn by
  Neuron::Int3 size{0, 0, 0};
  std::vector<ModuleRegion> regions;

  [[nodiscard]] const ModuleRegion* FindRegion(std::string_view _name) const noexcept;
  /// The one core region. Validation guarantees it exists.
  [[nodiscard]] const ModuleRegion* Core() const noexcept;
};

/// A mask a hull starts with: the master less these cells (an Assets/Hulls/<name>.txt preset).
struct DamagePreset
{
  std::string name;
  std::string hull;
  std::vector<Neuron::CellSpan> removed;
};

/// When a function weakens and stops, by its module's health in percent (Assets/Content/Modules.txt).
struct FunctionThreshold
{
  ModuleFunction function = ModuleFunction::Structure;
  int reducedBelowPercent = 0; // below this the module works at reducedRatePercent
  int stoppedBelowPercent = 0; // below this it does nothing
  int reducedRatePercent = 100;
};

struct HullContent
{
  std::vector<HullRecord> hulls;
  std::vector<DamagePreset> presets;
  std::vector<FunctionThreshold> thresholds;

  [[nodiscard]] const HullRecord* FindHull(std::string_view _name) const noexcept;
  /// The hull whose ships the scene places with this model, or null.
  [[nodiscard]] const HullRecord* HullForModel(std::string_view _legacyModel) const noexcept;
  [[nodiscard]] const DamagePreset* FindPreset(std::string_view _name) const noexcept;
  /// The thresholds for a function; a function without a record never weakens.
  [[nodiscard]] FunctionThreshold ThresholdFor(ModuleFunction _function) const noexcept;
};

/// Hulls.txt (ADR-021's grammar, loaded at run time since ADR-014): a catalog, hulls and regions.
/// Every region must lie inside its hull's grid, and every hull has exactly one core.
[[nodiscard]] bool ParseHulls(std::string_view _text, std::string_view _fileName, HullContent& _outContent, std::string& _outError);
/// A preset file: one preset record and its sorted, non-overlapping remove spans.
[[nodiscard]] bool ParsePreset(std::string_view _text, std::string_view _fileName, const HullContent& _hulls, DamagePreset& _outPreset,
                               std::string& _outError);
/// Modules.txt: one threshold record per function.
[[nodiscard]] bool ParseModuleThresholds(std::string_view _text, std::string_view _fileName, HullContent& _outContent,
                                         std::string& _outError);
/// Assets/Hulls/Hulls.txt, every other .txt in Assets/Hulls as a preset, and Assets/Content/Modules.txt.
[[nodiscard]] bool LoadHullContent(const std::filesystem::path& _assetRoot, HullContent& _outContent, std::string& _outError);

/// How well a module works.
enum class ModuleStatus : std::uint8_t
{
  Working,
  Reduced,
  Stopped
};

/// A module's health in whole percent, rounded down, so that both halves read the same number.
[[nodiscard]] int HealthPercent(std::uint32_t _present, std::uint32_t _total) noexcept;
[[nodiscard]] ModuleStatus StatusOf(const FunctionThreshold& _threshold, int _healthPercent) noexcept;
/// The rate a module works at, in percent: 100, the reduced rate, or 0.
[[nodiscard]] int RatePercent(const FunctionThreshold& _threshold, int _healthPercent) noexcept;

inline constexpr std::size_t MAX_MOUNTS = 4;

/// What a hull's modules leave a ship able to do, from their health as the server states it. Every
/// rate is an integer percent, so the client's prediction reads exactly the server's condition.
struct ShipCondition
{
  int engineRatePercent = 100; // the mean of the engine modules' rates
  int powerRatePercent = 100;  // the reactor's
  int shieldRatePercent = 100; // the shield generator's
  std::array<bool, MAX_MOUNTS> mountWorking{true, true, true, true};
  bool coreStopped = false;

  friend bool operator==(const ShipCondition&, const ShipCondition&) = default;
};

/// The condition a ship's stated module health gives it. _mountRegions names, per mount, the weapon
/// module it sits in. A module the list does not state counts as whole.
[[nodiscard]] ShipCondition ConditionOf(const HullContent& _content, const HullRecord& _hull, std::span<const ShipModuleState> _modules,
                                        std::span<const std::string> _mountRegions);

/// The hull's cells inside a region of these functions, as a mask over its grid.
[[nodiscard]] Neuron::VoxelMask RegionCells(const HullRecord& _hull, const Neuron::VoxelMask& _master,
                                            std::span<const ModuleFunction> _functions);
/// The cells outside every functional region: what a field repair may restore and what "hull" on
/// the HUD measures (GameDesign.md §7.1). Structure regions count as structure.
[[nodiscard]] Neuron::VoxelMask StructureCells(const HullRecord& _hull, const Neuron::VoxelMask& _master);

} // namespace Outpost
