#include "Character.h"

#include "SaveRecord.h"

namespace Outpost
{

namespace
{

[[nodiscard]] std::string_view CruiseText(CruiseState _cruise) noexcept
{
  switch (_cruise)
  {
  case CruiseState::Charging:
    return "charging";
  case CruiseState::Cruising:
    return "cruising";
  case CruiseState::Cooldown:
    return "cooldown";
  case CruiseState::Off:
    break;
  }
  return "off";
}

[[nodiscard]] bool ReadCruise(std::string_view _text, CruiseState& _out) noexcept
{
  for (const CruiseState state : {CruiseState::Off, CruiseState::Charging, CruiseState::Cruising, CruiseState::Cooldown})
  {
    if (_text == CruiseText(state))
    {
      _out = state;
      return true;
    }
  }
  return false;
}

void WriteShip(std::string_view _shipKeyword, std::string_view _moduleKeyword, const CharacterShip& _ship, Neuron::SaveWriter& _writer)
{
  _writer.Add(_shipKeyword).Field("instance", _ship.instance).Field("hull", SaveText(_ship.hullPercent));
  for (const ShipModuleState& module : _ship.modules)
  {
    _writer.Add(_moduleKeyword).Field("name", module.name).Field("health", SaveText(module.healthPercent));
  }
}

} // namespace

void WriteCharacter(const Character& _character, Neuron::SaveWriter& _writer)
{
  _writer.BeginSection(CHARACTER_SECTION);
  _writer.Add("character").Field("money", SaveText(_character.money));
  const CharacterLocation& location = _character.location;
  if (location.kind == LocationKind::Docked)
  {
    _writer.Add("location")
      .Field("kind", "docked")
      .Field("station", location.station)
      .Field("instance", location.instance)
      .Field("point", location.point);
  }
  else
  {
    const FlightState& flight = location.flight;
    _writer.Add("location")
      .Field("kind", "space")
      .Field("position", SaveText(flight.position))
      .Field("orientation", SaveText(flight.orientation))
      .Field("velocity", SaveText(flight.velocityMetresPerSecond))
      .Field("spin", SaveText(flight.angularVelocityRadiansPerSecond))
      .Field("power", SaveText(flight.powerPool))
      .Field("thrust", SaveText(flight.thrustPool))
      .Field("thruster", SaveText(flight.thrusterActive))
      .Field("cruise", CruiseText(flight.cruise))
      .Field("cruisetimer", SaveText(flight.cruiseTimerTicks))
      .Field("cruiseramp", SaveText(flight.cruiseRamp));
  }
  WriteShip("ship", "module", _character.ship, _writer);
  for (const CargoItem& cargo : _character.cargo)
  {
    _writer.Add("cargo").Field("item", cargo.item).Field("count", SaveText(cargo.count));
  }
  WriteShip("baseship", "basemodule", _character.baseSnapshot, _writer);
  for (const auto& [key, value] : _character.interfaceState)
  {
    _writer.Add("interface").Field("key", key).Field("value", value);
  }
}

bool ReadCharacter(const Neuron::SaveSection& _section, Character& _outCharacter, std::string& _outError)
{
  Character character;
  bool sawCharacter = false;
  bool sawLocation = false;
  bool sawShip = false;
  bool sawBaseShip = false;
  for (const Neuron::Record& record : _section.records)
  {
    SaveRecord reader(record, CHARACTER_SECTION, _outError);
    const auto once = [&reader](bool& _seen, std::string_view _keyword)
    {
      if (_seen)
      {
        return reader.Fail("a second '" + std::string(_keyword) + "' record");
      }
      _seen = true;
      return true;
    };
    if (record.keyword == "character")
    {
      if (once(sawCharacter, record.keyword) && reader.AllowOnly({"money"}))
      {
        reader.Read("money", character.money);
      }
    }
    else if (record.keyword == "location")
    {
      std::string kind;
      if (once(sawLocation, record.keyword))
      {
        reader.Read("kind", kind);
      }
      CharacterLocation& location = character.location;
      if (!reader.Ok())
      {
        return false;
      }
      if (kind == "docked")
      {
        location.kind = LocationKind::Docked;
        if (reader.AllowOnly({"kind", "station", "instance", "point"}))
        {
          reader.Read("station", location.station);
          reader.Read("instance", location.instance);
          reader.Read("point", location.point);
        }
      }
      else if (kind == "space")
      {
        location.kind = LocationKind::Space;
        FlightState& flight = location.flight;
        std::string cruise;
        if (reader.AllowOnly({"kind", "position", "orientation", "velocity", "spin", "power", "thrust", "thruster", "cruise", "cruisetimer",
                              "cruiseramp"}))
        {
          reader.Read("position", flight.position);
          reader.Read("orientation", flight.orientation);
          reader.Read("velocity", flight.velocityMetresPerSecond);
          reader.Read("spin", flight.angularVelocityRadiansPerSecond);
          reader.Read("power", flight.powerPool);
          reader.Read("thrust", flight.thrustPool);
          reader.Read("thruster", flight.thrusterActive);
          reader.Read("cruise", cruise);
          reader.Read("cruisetimer", flight.cruiseTimerTicks);
          reader.Read("cruiseramp", flight.cruiseRamp);
        }
        if (reader.Ok() && !ReadCruise(cruise, flight.cruise))
        {
          static_cast<void>(reader.Fail("'cruise' must be off, charging, cruising or cooldown"));
        }
      }
      else
      {
        static_cast<void>(reader.Fail("'kind' must be docked or space"));
      }
    }
    else if (record.keyword == "ship" || record.keyword == "baseship")
    {
      CharacterShip& ship = record.keyword == "ship" ? character.ship : character.baseSnapshot;
      if (once(record.keyword == "ship" ? sawShip : sawBaseShip, record.keyword) && reader.AllowOnly({"instance", "hull"}))
      {
        reader.Read("instance", ship.instance);
        reader.Read("hull", ship.hullPercent);
      }
    }
    else if (record.keyword == "module" || record.keyword == "basemodule")
    {
      const bool own = record.keyword == "module";
      ShipModuleState module;
      if (!(own ? sawShip : sawBaseShip))
      {
        static_cast<void>(reader.Fail("a '" + record.keyword + "' record before its ship"));
      }
      else if (reader.AllowOnly({"name", "health"}))
      {
        reader.Read("name", module.name);
        reader.Read("health", module.healthPercent);
        (own ? character.ship : character.baseSnapshot).modules.push_back(module);
      }
    }
    else if (record.keyword == "cargo")
    {
      CargoItem cargo;
      if (reader.AllowOnly({"item", "count"}))
      {
        reader.Read("item", cargo.item);
        reader.Read("count", cargo.count);
        character.cargo.push_back(cargo);
      }
    }
    else if (record.keyword == "interface")
    {
      std::pair<std::string, std::string> entry;
      if (reader.AllowOnly({"key", "value"}))
      {
        reader.Read("key", entry.first);
        reader.Read("value", entry.second);
        character.interfaceState.push_back(entry);
      }
    }
    else
    {
      static_cast<void>(reader.Fail("unknown record '" + record.keyword + "'"));
    }
    if (!reader.Ok())
    {
      return false;
    }
  }
  for (const auto& [seen, keyword] : {std::pair{sawCharacter, "character"}, std::pair{sawLocation, "location"}, std::pair{sawShip, "ship"},
                                      std::pair{sawBaseShip, "baseship"}})
  {
    if (!seen)
    {
      _outError = std::string(CHARACTER_SECTION) + ": no '" + keyword + "' record";
      return false;
    }
  }
  _outCharacter = std::move(character);
  return true;
}

} // namespace Outpost
