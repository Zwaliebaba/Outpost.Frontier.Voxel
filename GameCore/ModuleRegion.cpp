#include "ModuleRegion.h"

#include "FileSys.h"
#include "RecordFile.h"
#include "RecordReader.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <system_error>

namespace Outpost
{

namespace
{

struct FunctionName
{
  ModuleFunction function;
  std::string_view name;
};

constexpr std::array<FunctionName, 10> FUNCTION_NAMES{FunctionName{ModuleFunction::Core, "core"},
                                                      FunctionName{ModuleFunction::Power, "power"},
                                                      FunctionName{ModuleFunction::Shield, "shield"},
                                                      FunctionName{ModuleFunction::Engine, "engine"},
                                                      FunctionName{ModuleFunction::Sensor, "sensor"},
                                                      FunctionName{ModuleFunction::Weapon, "weapon"},
                                                      FunctionName{ModuleFunction::Atmosphere, "atmosphere"},
                                                      FunctionName{ModuleFunction::Structure, "structure"},
                                                      FunctionName{ModuleFunction::Clamp, "clamp"},
                                                      FunctionName{ModuleFunction::Cargo, "cargo"}};

/// "a,b,c" as three integers.
[[nodiscard]] bool ParseTriple(std::string_view _text, Neuron::Int3& _out) noexcept
{
  std::array<int, 3> values{};
  for (std::size_t i = 0; i < values.size(); ++i)
  {
    const std::size_t comma = _text.find(',');
    const std::string_view part = i + 1 < values.size() ? _text.substr(0, comma) : _text;
    if ((i + 1 < values.size() && comma == std::string_view::npos) || !Neuron::ParseInt(part, values[i]))
    {
      return false;
    }
    _text = comma == std::string_view::npos ? std::string_view() : _text.substr(comma + 1);
  }
  _out = Neuron::Int3{values[0], values[1], values[2]};
  return true;
}

[[nodiscard]] bool ReadTriple(RecordReader& _reader, std::string_view _key, Neuron::Int3& _out)
{
  std::string text;
  _reader.Reference(_key, text);
  if (_reader.Ok() && !ParseTriple(text, _out))
  {
    return _reader.Fail("'" + std::string(_key) + "' must be three integers");
  }
  return _reader.Ok();
}

[[nodiscard]] bool ReadCount(RecordReader& _reader, std::string_view _key, std::uint32_t& _out)
{
  std::string text;
  _reader.Reference(_key, text);
  std::uint64_t value = 0;
  if (_reader.Ok() && (!Neuron::ParseUInt64(text, value) || value > 0xFFFFFFFFull))
  {
    return _reader.Fail("'" + std::string(_key) + "' must be a whole number");
  }
  _out = static_cast<std::uint32_t>(value);
  return _reader.Ok();
}

[[nodiscard]] bool ReadPercent(RecordReader& _reader, std::string_view _key, int& _out)
{
  float value = 0.0f;
  _reader.Number(_key, 0.0f, 100.0f, value);
  _out = static_cast<int>(value);
  if (_reader.Ok() && static_cast<float>(_out) != value)
  {
    return _reader.Fail("'" + std::string(_key) + "' must be a whole percent");
  }
  return _reader.Ok();
}

} // namespace

std::optional<ModuleFunction> ParseModuleFunction(std::string_view _text) noexcept
{
  const auto found = std::ranges::find(FUNCTION_NAMES, _text, &FunctionName::name);
  return found != FUNCTION_NAMES.end() ? std::optional<ModuleFunction>(found->function) : std::nullopt;
}

std::string_view ModuleFunctionName(ModuleFunction _function) noexcept
{
  const auto found = std::ranges::find(FUNCTION_NAMES, _function, &FunctionName::function);
  return found != FUNCTION_NAMES.end() ? found->name : std::string_view("structure");
}

const ModuleRegion* HullRecord::FindRegion(std::string_view _name) const noexcept
{
  const auto found = std::ranges::find(regions, _name, &ModuleRegion::name);
  return found != regions.end() ? &*found : nullptr;
}

const ModuleRegion* HullRecord::Core() const noexcept
{
  const auto found = std::ranges::find(regions, ModuleFunction::Core, &ModuleRegion::function);
  return found != regions.end() ? &*found : nullptr;
}

const HullRecord* HullContent::FindHull(std::string_view _name) const noexcept
{
  const auto found = std::ranges::find(hulls, _name, &HullRecord::name);
  return found != hulls.end() ? &*found : nullptr;
}

const HullRecord* HullContent::HullForModel(std::string_view _legacyModel) const noexcept
{
  const auto found = std::ranges::find(hulls, _legacyModel, &HullRecord::legacy);
  return found != hulls.end() ? &*found : nullptr;
}

const DamagePreset* HullContent::FindPreset(std::string_view _name) const noexcept
{
  const auto found = std::ranges::find(presets, _name, &DamagePreset::name);
  return found != presets.end() ? &*found : nullptr;
}

FunctionThreshold HullContent::ThresholdFor(ModuleFunction _function) const noexcept
{
  const auto found = std::ranges::find(thresholds, _function, &FunctionThreshold::function);
  return found != thresholds.end() ? *found : FunctionThreshold{_function, 0, 0, 100};
}

bool ParseHulls(std::string_view _text, std::string_view _fileName, HullContent& _outContent, std::string& _outError)
{
  std::vector<Neuron::Record> records;
  if (!Neuron::ParseRecords(_text, records, _outError))
  {
    _outError = std::string(_fileName) + ", " + _outError;
    return false;
  }
  std::vector<HullRecord> hulls;
  std::vector<int> hullLines;
  for (const Neuron::Record& record : records)
  {
    RecordReader reader(record, _fileName, _outError);
    if (record.keyword == "catalog")
    {
      if (!reader.AllowOnly({"name", "version"}))
      {
        return false;
      }
    }
    else if (record.keyword == "hull")
    {
      HullRecord hull;
      if (reader.AllowOnly({"name", "model", "legacy", "size", "sha256"}))
      {
        reader.Name(hull.name);
        reader.Reference("model", hull.master);
        reader.Reference("legacy", hull.legacy);
      }
      if (!reader.Ok() || !ReadTriple(reader, "size", hull.size))
      {
        return false;
      }
      if (std::ranges::find(hulls, hull.name, &HullRecord::name) != hulls.end())
      {
        return reader.Fail("'" + hull.name + "' is defined twice");
      }
      hulls.push_back(std::move(hull));
      hullLines.push_back(record.line);
    }
    else if (record.keyword == "region")
    {
      ModuleRegion region;
      std::string hullName;
      std::string function;
      if (reader.AllowOnly({"hull", "name", "function", "min", "max"}))
      {
        reader.Reference("hull", hullName);
        reader.Name(region.name);
        reader.Reference("function", function);
      }
      if (!reader.Ok() || !ReadTriple(reader, "min", region.min) || !ReadTriple(reader, "max", region.max))
      {
        return false;
      }
      const std::optional<ModuleFunction> parsed = ParseModuleFunction(function);
      if (!parsed.has_value())
      {
        return reader.Fail("'function' must be core, power, shield, engine, sensor, weapon, atmosphere, structure, clamp or cargo");
      }
      region.function = *parsed;
      const auto hull = std::ranges::find(hulls, hullName, &HullRecord::name);
      if (hull == hulls.end())
      {
        return reader.Fail("region names no hull '" + hullName + "' defined above it");
      }
      const bool inside = region.min.x >= 0 && region.min.y >= 0 && region.min.z >= 0 && region.max.x <= hull->size.x &&
                          region.max.y <= hull->size.y && region.max.z <= hull->size.z && region.min.x < region.max.x &&
                          region.min.y < region.max.y && region.min.z < region.max.z;
      if (!inside)
      {
        return reader.Fail("region '" + region.name + "' is empty or leaves hull '" + hullName + "'");
      }
      if (hull->FindRegion(region.name) != nullptr)
      {
        return reader.Fail("hull '" + hullName + "' has two regions named '" + region.name + "'");
      }
      hull->regions.push_back(std::move(region));
    }
    else
    {
      return reader.Fail("unknown keyword '" + record.keyword + "'");
    }
  }
  for (std::size_t i = 0; i < hulls.size(); ++i)
  {
    const auto cores = std::ranges::count(hulls[i].regions, ModuleFunction::Core, &ModuleRegion::function);
    if (cores != 1)
    {
      _outError = std::string(_fileName) + " line " + std::to_string(hullLines[i]) + ": hull '" + hulls[i].name + "' has " +
                  std::to_string(cores) + " core regions, where it must have exactly one";
      return false;
    }
  }
  _outContent.hulls = std::move(hulls);
  return true;
}

bool ParsePreset(std::string_view _text, std::string_view _fileName, const HullContent& _hulls, DamagePreset& _outPreset,
                 std::string& _outError)
{
  std::vector<Neuron::Record> records;
  if (!Neuron::ParseRecords(_text, records, _outError))
  {
    _outError = std::string(_fileName) + ", " + _outError;
    return false;
  }
  DamagePreset preset;
  bool named = false;
  std::uint64_t cellCount = 0;
  for (const Neuron::Record& record : records)
  {
    RecordReader reader(record, _fileName, _outError);
    if (record.keyword == "preset")
    {
      if (named)
      {
        return reader.Fail("a preset file holds one preset");
      }
      if (reader.AllowOnly({"name", "version", "hull", "preview", "mastersha256", "previewsha256"}))
      {
        reader.Name(preset.name);
        reader.Reference("hull", preset.hull);
      }
      if (!reader.Ok())
      {
        return false;
      }
      const HullRecord* hull = _hulls.FindHull(preset.hull);
      if (hull == nullptr)
      {
        return reader.Fail("preset names no known hull '" + preset.hull + "'");
      }
      cellCount =
        static_cast<std::uint64_t>(hull->size.x) * static_cast<std::uint64_t>(hull->size.y) * static_cast<std::uint64_t>(hull->size.z);
      named = true;
    }
    else if (record.keyword == "remove")
    {
      Neuron::CellSpan span;
      if (!named)
      {
        return reader.Fail("a remove record before the preset record");
      }
      if (!reader.AllowOnly({"start", "count"}) || !ReadCount(reader, "start", span.start) || !ReadCount(reader, "count", span.count))
      {
        return false;
      }
      const bool ordered = preset.removed.empty() || span.start >= preset.removed.back().start + preset.removed.back().count;
      if (span.count == 0 || static_cast<std::uint64_t>(span.start) + span.count > cellCount || !ordered)
      {
        return reader.Fail("remove spans must be non-empty, inside the hull's grid, sorted and apart");
      }
      preset.removed.push_back(span);
    }
    else if (record.keyword == "appearance")
    {
      // Presentation: the cosmetic overlay the client draws over the mask (ADR-021). The server and
      // the mask never read it.
      if (!reader.AllowOnly({"model", "sha256"}))
      {
        return false;
      }
    }
    else
    {
      return reader.Fail("unknown keyword '" + record.keyword + "'");
    }
  }
  if (!named)
  {
    _outError = std::string(_fileName) + ": no preset record";
    return false;
  }
  _outPreset = std::move(preset);
  return true;
}

bool ParseModuleThresholds(std::string_view _text, std::string_view _fileName, HullContent& _outContent, std::string& _outError)
{
  std::vector<Neuron::Record> records;
  if (!Neuron::ParseRecords(_text, records, _outError))
  {
    _outError = std::string(_fileName) + ", " + _outError;
    return false;
  }
  std::vector<FunctionThreshold> thresholds;
  for (const Neuron::Record& record : records)
  {
    RecordReader reader(record, _fileName, _outError);
    if (record.keyword != "function")
    {
      return reader.Fail("unknown keyword '" + record.keyword + "'");
    }
    FunctionThreshold threshold;
    std::string name;
    if (reader.AllowOnly({"name", "reduced", "stopped", "reducedrate"}))
    {
      reader.Reference("name", name);
    }
    if (!reader.Ok() || !ReadPercent(reader, "reduced", threshold.reducedBelowPercent) ||
        !ReadPercent(reader, "stopped", threshold.stoppedBelowPercent) || !ReadPercent(reader, "reducedrate", threshold.reducedRatePercent))
    {
      return false;
    }
    const std::optional<ModuleFunction> function = ParseModuleFunction(name);
    if (!function.has_value())
    {
      return reader.Fail("no function '" + name + "'");
    }
    threshold.function = *function;
    if (threshold.stoppedBelowPercent > threshold.reducedBelowPercent)
    {
      return reader.Fail("a function stops below no more than where it weakens");
    }
    if (std::ranges::find(thresholds, threshold.function, &FunctionThreshold::function) != thresholds.end())
    {
      return reader.Fail("function '" + name + "' has two records");
    }
    thresholds.push_back(threshold);
  }
  _outContent.thresholds = std::move(thresholds);
  return true;
}

bool LoadHullContent(const std::filesystem::path& _assetRoot, HullContent& _outContent, std::string& _outError)
{
  HullContent content;
  std::string text;
  const std::filesystem::path hulls = _assetRoot / "Hulls" / "Hulls.txt";
  if (!Neuron::ReadTextFile(hulls, text))
  {
    _outError = "cannot read " + hulls.string();
    return false;
  }
  if (!ParseHulls(text, "Hulls.txt", content, _outError))
  {
    return false;
  }
  // Presets in name order, so the same content always loads the same way.
  std::vector<std::filesystem::path> presetFiles;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(_assetRoot / "Hulls", error))
  {
    if (entry.path().extension() == ".txt" && entry.path().filename() != "Hulls.txt")
    {
      presetFiles.push_back(entry.path());
    }
  }
  std::ranges::sort(presetFiles);
  for (const std::filesystem::path& file : presetFiles)
  {
    DamagePreset preset;
    if (!Neuron::ReadTextFile(file, text) || !ParsePreset(text, file.filename().string(), content, preset, _outError))
    {
      _outError = _outError.empty() ? "cannot read " + file.string() : _outError;
      return false;
    }
    if (content.FindPreset(preset.name) != nullptr)
    {
      _outError = file.filename().string() + ": preset '" + preset.name + "' is defined twice";
      return false;
    }
    if (file.stem().string() != preset.name)
    {
      _outError = file.filename().string() + ": the preset in it must be named for the file, '" + file.stem().string() + "'";
      return false;
    }
    content.presets.push_back(std::move(preset));
  }
  const std::filesystem::path modules = _assetRoot / "Content" / "Modules.txt";
  if (!Neuron::ReadTextFile(modules, text))
  {
    _outError = "cannot read " + modules.string();
    return false;
  }
  if (!ParseModuleThresholds(text, "Modules.txt", content, _outError))
  {
    return false;
  }
  _outContent = std::move(content);
  return true;
}

int HealthPercent(std::uint32_t _present, std::uint32_t _total) noexcept
{
  if (_total == 0)
  {
    return 100;
  }
  return static_cast<int>(static_cast<std::uint64_t>(_present) * 100u / _total);
}

ModuleStatus StatusOf(const FunctionThreshold& _threshold, int _healthPercent) noexcept
{
  if (_healthPercent < _threshold.stoppedBelowPercent || _healthPercent == 0)
  {
    return ModuleStatus::Stopped;
  }
  if (_healthPercent < _threshold.reducedBelowPercent)
  {
    return ModuleStatus::Reduced;
  }
  return ModuleStatus::Working;
}

int RatePercent(const FunctionThreshold& _threshold, int _healthPercent) noexcept
{
  switch (StatusOf(_threshold, _healthPercent))
  {
  case ModuleStatus::Stopped:
    return 0;
  case ModuleStatus::Reduced:
    return _threshold.reducedRatePercent;
  case ModuleStatus::Working:
    break;
  }
  return 100;
}

ShipCondition ConditionOf(const HullContent& _content, const HullRecord& _hull, std::span<const ShipModuleState> _modules,
                          std::span<const std::string> _mountRegions)
{
  ShipCondition condition;
  const auto healthOf = [&](std::string_view _name)
  {
    const auto found = std::ranges::find(_modules, _name, &ShipModuleState::name);
    return found != _modules.end() ? found->healthPercent : 100;
  };
  int engineSum = 0;
  int engineCount = 0;
  for (const ModuleRegion& region : _hull.regions)
  {
    const int rate = RatePercent(_content.ThresholdFor(region.function), healthOf(region.name));
    switch (region.function)
    {
    case ModuleFunction::Core:
      condition.coreStopped = rate == 0;
      break;
    case ModuleFunction::Engine:
      engineSum += rate;
      ++engineCount;
      break;
    case ModuleFunction::Power:
      condition.powerRatePercent = std::min(condition.powerRatePercent, rate);
      break;
    case ModuleFunction::Shield:
      condition.shieldRatePercent = std::min(condition.shieldRatePercent, rate);
      break;
    default:
      break;
    }
  }
  if (engineCount > 0)
  {
    condition.engineRatePercent = engineSum / engineCount;
  }
  for (std::size_t mount = 0; mount < _mountRegions.size() && mount < MAX_MOUNTS; ++mount)
  {
    const ModuleRegion* region = _hull.FindRegion(_mountRegions[mount]);
    condition.mountWorking[mount] = region == nullptr || RatePercent(_content.ThresholdFor(region->function), healthOf(region->name)) > 0;
  }
  return condition;
}

Neuron::VoxelMask RegionCells(const HullRecord& _hull, const Neuron::VoxelMask& _master, std::span<const ModuleFunction> _functions)
{
  Neuron::VoxelMask cells(_master.Size());
  for (const ModuleRegion& region : _hull.regions)
  {
    if (std::ranges::find(_functions, region.function) == _functions.end())
    {
      continue;
    }
    for (int z = region.min.z; z < region.max.z; ++z)
    {
      for (int y = region.min.y; y < region.max.y; ++y)
      {
        for (int x = region.min.x; x < region.max.x; ++x)
        {
          if (_master.Present(x, y, z))
          {
            cells.SetAt(_master.Index(x, y, z), true);
          }
        }
      }
    }
  }
  return cells;
}

Neuron::VoxelMask StructureCells(const HullRecord& _hull, const Neuron::VoxelMask& _master)
{
  constexpr std::array<ModuleFunction, 9> FUNCTIONAL{ModuleFunction::Core,       ModuleFunction::Power,  ModuleFunction::Shield,
                                                     ModuleFunction::Engine,     ModuleFunction::Sensor, ModuleFunction::Weapon,
                                                     ModuleFunction::Atmosphere, ModuleFunction::Clamp,  ModuleFunction::Cargo};
  const Neuron::VoxelMask functional = RegionCells(_hull, _master, FUNCTIONAL);
  Neuron::VoxelMask structure(_master.Size());
  for (std::uint32_t i = 0; i < _master.CellCount(); ++i)
  {
    structure.SetAt(i, _master.PresentAt(i) && !functional.PresentAt(i));
  }
  return structure;
}

} // namespace Outpost
