#include "MaskEvent.h"

#include "Palette.h"
#include "SceneDefinition.h"
#include "VoxFile.h"

#include <algorithm>

namespace Outpost
{

const VoxelArchetype* CombatContent::FindArchetype(std::string_view _model) const noexcept
{
  const auto found = std::ranges::find(archetypes, _model, &VoxelArchetype::model);
  return found != archetypes.end() ? &*found : nullptr;
}

const HullRecord* CombatContent::HullOf(const VoxelArchetype& _archetype) const noexcept
{
  return _archetype.hull >= 0 ? &hulls.hulls[static_cast<std::size_t>(_archetype.hull)] : nullptr;
}

bool LoadCombatContent(const std::filesystem::path& _assetRoot, const SceneDefinition& _scene, CombatContent& _outContent,
                       std::string& _outError)
{
  CombatContent content;
  if (!LoadHullContent(_assetRoot, content.hulls, _outError) ||
      !LoadPilots(_assetRoot / "Content" / "Pilots.txt", content.pilots, _outError))
  {
    return false;
  }
  for (const ModelAsset& model : _scene.models)
  {
    VoxelArchetype archetype;
    archetype.model = model.name;
    const HullRecord* hull = content.hulls.HullForModel(model.name);
    const std::filesystem::path file = hull != nullptr ? _assetRoot / "Models" / (hull->master + ".vox") : _assetRoot / model.file;
    Neuron::VoxDocument document;
    const Neuron::VoxError error = Neuron::LoadVoxFile(file, document);
    if (error != Neuron::VoxError::None)
    {
      _outError = "cannot load " + file.string() + ": " + Neuron::VoxErrorName(error);
      return false;
    }
    archetype.voxels = std::move(document.model);
    archetype.full = Neuron::VoxelMask::FromModel(archetype.voxels);
    if (hull != nullptr)
    {
      const Neuron::Int3 size = archetype.full.Size();
      if (!(size == hull->size) || !(size == model.size))
      {
        _outError = "hull '" + hull->name + "': master " + hull->master + " is not the grid of model '" + model.name + "'";
        return false;
      }
      archetype.hull = static_cast<int>(hull - content.hulls.hulls.data());
      const ModuleRegion* core = hull->Core();
      for (int z = core->min.z; z < core->max.z; ++z)
      {
        for (int y = core->min.y; y < core->max.y; ++y)
        {
          for (int x = core->min.x; x < core->max.x; ++x)
          {
            if (archetype.full.Present(x, y, z))
            {
              archetype.coreCells.push_back(archetype.full.Index(x, y, z));
            }
          }
        }
      }
      archetype.structure = StructureCells(*hull, archetype.full);
      archetype.modules = Neuron::VoxelMask(size);
      for (std::uint32_t i = 0; i < archetype.full.CellCount(); ++i)
      {
        archetype.modules.SetAt(i, archetype.full.PresentAt(i) && !archetype.structure.PresentAt(i));
      }
    }
    else
    {
      archetype.structure = archetype.full;
      archetype.modules = Neuron::VoxelMask(archetype.full.Size());
    }
    content.archetypes.push_back(std::move(archetype));
  }
  _outContent = std::move(content);
  return true;
}

Neuron::VoxelMask InitialMask(const CombatContent& _content, const VoxelArchetype& _archetype, std::string_view _preset)
{
  const DamagePreset* preset = _preset.empty() ? nullptr : _content.hulls.FindPreset(_preset);
  Neuron::VoxelMask mask = _archetype.full;
  if (preset != nullptr && !Neuron::MaskFromAbsentSpans(_archetype.full, preset->removed, mask))
  {
    return _archetype.full;
  }
  return mask;
}

MaskEventResult ApplyMaskEvent(const VoxelArchetype& _archetype, const MaskEvent& _event, Neuron::VoxelMask& _mask)
{
  MaskEventResult result;
  if (_event.kind == MaskEventKind::Patch)
  {
    result.restored = Neuron::Patch(_mask, _archetype.structure, _event.patch);
    return result;
  }
  Neuron::CarveResult carve = Neuron::Carve(_mask, _archetype.voxels, MaterialStrengths(), _event.carve,
                                            _event.toughened ? &_archetype.modules : nullptr, PLAYER_MODULE_TOUGHNESS);
  result.removed = std::move(carve.removed);
  if (result.removed.empty() || _archetype.coreCells.empty())
  {
    return result;
  }
  result.detached = Neuron::DetachedParts(_mask, _archetype.coreCells, result.removed);
  // With the core gone entirely, nothing is attached to anything: the ship's death, which the
  // authority decides from the core's health, breaks it up. Until then only cut-off parts go.
  const bool coreLeft = std::ranges::any_of(_archetype.coreCells, [&](std::uint32_t _cell) { return _mask.PresentAt(_cell); });
  if (!coreLeft)
  {
    result.detached.clear();
    return result;
  }
  for (const std::vector<std::uint32_t>& part : result.detached)
  {
    for (const std::uint32_t cell : part)
    {
      _mask.SetAt(cell, false);
    }
  }
  return result;
}

} // namespace Outpost
