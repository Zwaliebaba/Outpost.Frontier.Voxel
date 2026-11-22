#include "ShipArchetype.h"

#include "FileSys.h"
#include "RecordFile.h"
#include "RecordReader.h"
#include "SceneDefinition.h"

#include <algorithm>
#include <initializer_list>
#include <optional>

namespace Outpost
{

namespace
{

// Wide enough for every hull the blueprint tabulates, NPC capital ships included (ch. 02 §4).
constexpr float MAX_MASS_KILOGRAMS = 1.0e7f;
constexpr float MAX_LARGE_QUANTITY = 1.0e11f;

template <typename T> [[nodiscard]] const T* FindByName(const std::vector<T>& _items, std::string_view _name) noexcept
{
  const auto found = std::ranges::find(_items, _name, &T::name);
  return found != _items.end() ? &*found : nullptr;
}

template <typename T> [[nodiscard]] bool AddUnique(std::vector<T>& _items, T _item, RecordReader& _reader)
{
  if (FindByName(_items, _item.name) != nullptr)
  {
    return _reader.Fail("'" + _item.name + "' is defined twice");
  }
  _items.push_back(std::move(_item));
  return true;
}

} // namespace

const EngineArchetype* ShipContent::FindEngine(std::string_view _name) const noexcept
{
  return FindByName(engines, _name);
}

const PowerPlantArchetype* ShipContent::FindPowerPlant(std::string_view _name) const noexcept
{
  return FindByName(powerPlants, _name);
}

const ThrusterArchetype* ShipContent::FindThruster(std::string_view _name) const noexcept
{
  return FindByName(thrusters, _name);
}

const ShipArchetype* ShipContent::FindShip(std::string_view _name) const noexcept
{
  return FindByName(ships, _name);
}

const ChaseCameraRecord* ShipContent::FindChaseCamera(std::string_view _ship) const noexcept
{
  const auto found = std::ranges::find(chaseCameras, _ship, &ChaseCameraRecord::ship);
  return found != chaseCameras.end() ? &*found : nullptr;
}

bool ParseShipContent(std::string_view _text, std::string_view _fileName, ShipContent& _outContent, std::string& _outError)
{
  std::vector<Neuron::Record> records;
  if (!Neuron::ParseRecords(_text, records, _outError))
  {
    _outError = std::string(_fileName) + ", " + _outError;
    return false;
  }
  ShipContent content;
  std::vector<int> shipLines;
  std::vector<int> chaseLines;
  for (const Neuron::Record& record : records)
  {
    RecordReader reader(record, _fileName, _outError);
    if (record.keyword == "engine")
    {
      EngineArchetype engine;
      if (reader.AllowOnly({"name", "thrust", "drag", "reverse", "chargetime", "cruisepower"}))
      {
        reader.Name(engine.name);
        reader.Number("thrust", 0.0f, MAX_LARGE_QUANTITY, engine.thrustNewtons);
        reader.Number("drag", 0.0f, MAX_LARGE_QUANTITY, engine.dragKilogramsPerSecond);
        reader.Number("reverse", 0.0f, 1.0f, engine.reverseFraction);
        reader.Number("chargetime", 0.0f, 60.0f, engine.cruiseChargeSeconds);
        reader.Number("cruisepower", 0.0f, MAX_LARGE_QUANTITY, engine.cruisePowerPerSecond);
      }
      if (!reader.Ok() || !AddUnique(content.engines, std::move(engine), reader))
      {
        return false;
      }
    }
    else if (record.keyword == "powerplant")
    {
      PowerPlantArchetype plant;
      if (reader.AllowOnly({"name", "capacity", "charge", "thrustcapacity", "thrustcharge"}))
      {
        reader.Name(plant.name);
        reader.Number("capacity", 0.0f, MAX_LARGE_QUANTITY, plant.powerCapacity);
        reader.Number("charge", 0.0f, MAX_LARGE_QUANTITY, plant.powerChargePerSecond);
        reader.Number("thrustcapacity", 0.0f, MAX_LARGE_QUANTITY, plant.thrustCapacity);
        reader.Number("thrustcharge", 0.0f, MAX_LARGE_QUANTITY, plant.thrustChargePerSecond);
      }
      if (!reader.Ok() || !AddUnique(content.powerPlants, std::move(plant), reader))
      {
        return false;
      }
    }
    else if (record.keyword == "thruster")
    {
      ThrusterArchetype thruster;
      if (reader.AllowOnly({"name", "force", "drain"}))
      {
        reader.Name(thruster.name);
        reader.Number("force", 0.0f, MAX_LARGE_QUANTITY, thruster.forceNewtons);
        reader.Number("drain", 0.0f, MAX_LARGE_QUANTITY, thruster.drainPerSecond);
      }
      if (!reader.Ok() || !AddUnique(content.thrusters, std::move(thruster), reader))
      {
        return false;
      }
    }
    else if (record.keyword == "ship")
    {
      ShipArchetype ship;
      if (reader.AllowOnly({"name", "model", "mass", "inertia", "drag", "torque", "angulardrag", "strafe", "strafedrain", "maxbank",
                            "engine", "powerplant", "thruster", "docking"}))
      {
        reader.Name(ship.name);
        reader.Reference("model", ship.model);
        // Mass, inertia and angular drag divide: they must be positive.
        reader.Number("mass", 0.001f, MAX_MASS_KILOGRAMS, ship.massKilograms);
        reader.Vector("inertia", 0.001f, MAX_LARGE_QUANTITY, ship.inertiaKilogramSquareMetres);
        reader.Number("drag", 0.0f, MAX_LARGE_QUANTITY, ship.hullDragKilogramsPerSecond);
        reader.Vector("torque", 0.0f, MAX_LARGE_QUANTITY, ship.steeringTorqueNewtonMetres);
        reader.Vector("angulardrag", 0.001f, MAX_LARGE_QUANTITY, ship.angularDragNewtonMetreSeconds);
        reader.Number("strafe", 0.0f, MAX_LARGE_QUANTITY, ship.strafeForceNewtons);
        reader.Number("strafedrain", 0.0f, MAX_LARGE_QUANTITY, ship.strafeDrainPerSecond);
        reader.Number("maxbank", 0.0f, 90.0f, ship.maxBankDegrees);
        reader.Reference("engine", ship.engine);
        reader.Reference("powerplant", ship.powerPlant);
        reader.Reference("thruster", ship.thruster);
        std::string docking;
        reader.Reference("docking", docking);
        const std::optional<DockKind> dockingClass = ParseDockKind(docking);
        if (reader.Ok() && !dockingClass.has_value())
        {
          static_cast<void>(reader.Fail("'docking' must be berth, medium or large"));
        }
        ship.dockingClass = dockingClass.value_or(DockKind::Berth);
      }
      if (!reader.Ok() || !AddUnique(content.ships, std::move(ship), reader))
      {
        return false;
      }
      shipLines.push_back(record.line);
    }
    else if (record.keyword == "chase")
    {
      ChaseCameraRecord chase;
      if (reader.AllowOnly({"ship", "up", "back", "lookside", "lookup", "lookdown"}))
      {
        reader.Reference("ship", chase.ship);
        reader.Number("up", 0.0f, 1000.0f, chase.upMetres);
        reader.Number("back", 1.0f, 1000.0f, chase.backMetres);
        reader.Number("lookside", 0.0f, 60.0f, chase.lookSideDegrees);
        reader.Number("lookup", 0.0f, 60.0f, chase.lookUpDegrees);
        reader.Number("lookdown", 0.0f, 60.0f, chase.lookDownDegrees);
      }
      if (!reader.Ok())
      {
        return false;
      }
      if (content.FindChaseCamera(chase.ship) != nullptr)
      {
        return reader.Fail("ship '" + chase.ship + "' has two chase cameras");
      }
      content.chaseCameras.push_back(std::move(chase));
      chaseLines.push_back(record.line);
    }
    else
    {
      return reader.Fail("unknown keyword '" + record.keyword + "'");
    }
  }

  // References are resolved once the whole file is read, so a ship may name equipment defined below it.
  for (std::size_t i = 0; i < content.ships.size(); ++i)
  {
    const ShipArchetype& ship = content.ships[i];
    const char* dangling = content.FindEngine(ship.engine) == nullptr           ? "engine"
                           : content.FindPowerPlant(ship.powerPlant) == nullptr ? "power plant"
                           : content.FindThruster(ship.thruster) == nullptr     ? "thruster"
                                                                                : nullptr;
    if (dangling != nullptr)
    {
      _outError =
        std::string(_fileName) + " line " + std::to_string(shipLines[i]) + ": ship '" + ship.name + "' names no known " + dangling;
      return false;
    }
  }
  for (std::size_t i = 0; i < content.chaseCameras.size(); ++i)
  {
    if (content.FindShip(content.chaseCameras[i].ship) == nullptr)
    {
      _outError = std::string(_fileName) + " line " + std::to_string(chaseLines[i]) + ": chase camera names no known ship '" +
                  content.chaseCameras[i].ship + "'";
      return false;
    }
  }
  _outContent = std::move(content);
  return true;
}

bool LoadShipContent(const std::filesystem::path& _path, ShipContent& _outContent, std::string& _outError)
{
  std::string text;
  if (!Neuron::ReadTextFile(_path, text))
  {
    _outError = "cannot read " + _path.string();
    return false;
  }
  return ParseShipContent(text, _path.filename().string(), _outContent, _outError);
}

std::vector<std::string> ResolveShipModels(const ShipContent& _content, const SceneDefinition& _scene)
{
  std::vector<std::string> problems;
  for (const ShipArchetype& ship : _content.ships)
  {
    if (_scene.FindModel(ship.model) == nullptr)
    {
      problems.push_back("ship '" + ship.name + "' names model '" + ship.model + "', which the scene does not define");
    }
  }
  return problems;
}

} // namespace Outpost
