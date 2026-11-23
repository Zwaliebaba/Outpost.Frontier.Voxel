#pragma once

#include "ModuleRegion.h"
#include "Pilot.h"
#include "UniverseObject.h"
#include "VoxModel.h"
#include "VoxelCarve.h"
#include "VoxelMask.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

struct SceneDefinition;

// The damage event and its application (ADR-012, AGENTS.md R22). The authority decides an event;
// ApplyMaskEvent is the one function both halves apply it with, so client and server arrive at the
// same voxels. Everything it reads is content both halves load, the event and the mask.

/// One model's voxels as the masks of its objects are taken against: the intact master of a hull,
/// or the scene model itself for an object with no hull (ADR-014).
struct VoxelArchetype
{
  std::string model;                    // the scene model that places and draws the objects
  Neuron::VoxModel voxels;              // colours, and through them materials
  Neuron::VoxelMask full;               // every solid cell
  int hull = -1;                        // index into CombatContent::hulls.hulls, or -1
  std::vector<std::uint32_t> coreCells; // the core region's solid cells; empty without a hull
  Neuron::VoxelMask modules;            // the cells of functional regions: a player's are tougher (GameDesign.md §7.1)
  Neuron::VoxelMask structure;          // the cells outside them: what a field repair restores (plan task 3.11)
};

/// What the combat of both halves loads (plan tasks 3.1 to 3.3, 3.8).
struct CombatContent
{
  HullContent hulls;
  PilotContent pilots;
  std::vector<VoxelArchetype> archetypes; // one per scene model, in the scene's order

  [[nodiscard]] const VoxelArchetype* FindArchetype(std::string_view _model) const noexcept;
  [[nodiscard]] const HullRecord* HullOf(const VoxelArchetype& _archetype) const noexcept;
};

/// Assets/Hulls, Content/Modules.txt, Content/Pilots.txt, and the voxels of every scene model, or of
/// its hull's master where it has one. A master must have its scene model's grid.
[[nodiscard]] bool LoadCombatContent(const std::filesystem::path& _assetRoot, const SceneDefinition& _scene, CombatContent& _outContent,
                                     std::string& _outError);

/// The mask an object starts with: its archetype, less its preset's cells.
[[nodiscard]] Neuron::VoxelMask InitialMask(const CombatContent& _content, const VoxelArchetype& _archetype, std::string_view _preset);

enum class MaskEventKind : std::uint8_t
{
  Carve, // damage: removes cells around an impact
  Patch  // repair: restores structure
};

/// Why a mask changed, carried for presentation and for those who listen (chapter 03's causes).
enum class DamageCause : std::uint8_t
{
  Weapon,
  Collision,
  Hazard,
  Repair
};

/// One mask event as the authority issues it (ADR-012).
struct MaskEvent
{
  ObjectId target = NO_OBJECT;
  MaskEventKind kind = MaskEventKind::Carve;
  Neuron::CarveSpec carve;
  Neuron::PatchSpec patch;
  bool toughened = false; // the target is a player's: its module cells cost PLAYER_MODULE_TOUGHNESS times as much
  DamageCause cause = DamageCause::Weapon;
  ObjectId inflictor = NO_OBJECT;

  friend bool operator==(const MaskEvent&, const MaskEvent&) = default;
};

struct MaskEventResult
{
  std::vector<std::uint32_t> removed;  // carved away by the event
  std::vector<std::uint32_t> restored; // patched back by it
  // The parts the event cut from the core, each removed from the mask as part of applying it, to
  // become wrecks. Only an archetype with a core sheds parts.
  std::vector<std::vector<std::uint32_t>> detached;
};

/// Applies an event to a mask of the archetype: the carve or the patch, then, for a carve on an
/// archetype with a core, the parts no longer attached to it are taken out of the mask.
MaskEventResult ApplyMaskEvent(const VoxelArchetype& _archetype, const MaskEvent& _event, Neuron::VoxelMask& _mask);

} // namespace Outpost
