#include "Pilot.h"

#include "FileSys.h"
#include "RecordFile.h"
#include "RecordReader.h"

#include <algorithm>

namespace Outpost
{

const PilotRecord* PilotContent::FindPilot(std::string_view _name) const noexcept
{
  const auto found = std::ranges::find(pilots, _name, &PilotRecord::name);
  return found != pilots.end() ? &*found : nullptr;
}

bool ParsePilots(std::string_view _text, std::string_view _fileName, PilotContent& _outContent, std::string& _outError)
{
  std::vector<Neuron::Record> records;
  if (!Neuron::ParseRecords(_text, records, _outError))
  {
    _outError = std::string(_fileName) + ", " + _outError;
    return false;
  }
  PilotContent content;
  for (const Neuron::Record& record : records)
  {
    RecordReader reader(record, _fileName, _outError);
    if (record.keyword != "pilot")
    {
      return reader.Fail("unknown keyword '" + record.keyword + "'");
    }
    PilotRecord pilot;
    float fleeCore = 0.0f;
    float fleeHull = 0.0f;
    if (reader.AllowOnly({"name", "detection", "turnin", "pass", "passtime", "extendtime", "trail", "burst", "pause", "cone", "exponent",
                          "rerollmin", "rerollmax", "rangefactor", "evade", "dodge", "flee", "fleehull", "gone"}))
    {
      reader.Name(pilot.name);
      reader.Number("detection", 0.0f, 1.0e6f, pilot.detectionMetres);
      reader.Number("turnin", 0.0f, 1.0e5f, pilot.turnInMetres);
      reader.Number("pass", 0.0f, 1.0e5f, pilot.passMetres);
      reader.Number("passtime", 0.0f, 60.0f, pilot.passSeconds);
      reader.Number("extendtime", 0.0f, 60.0f, pilot.extendSeconds);
      reader.Number("trail", 0.0f, 1.0e5f, pilot.trailMetres);
      reader.Number("burst", 0.05f, 60.0f, pilot.burstSeconds);
      reader.Number("pause", 0.0f, 60.0f, pilot.pauseSeconds);
      reader.Number("cone", 0.0f, 45.0f, pilot.aimConeDegrees);
      reader.Number("exponent", 0.1f, 10.0f, pilot.aimExponent);
      reader.Number("rerollmin", 0.05f, 60.0f, pilot.rerollMinSeconds);
      reader.Number("rerollmax", 0.05f, 60.0f, pilot.rerollMaxSeconds);
      reader.Number("rangefactor", 0.1f, 10.0f, pilot.rangeFactor);
      reader.Number("evade", 0.0f, 1.0e5f, pilot.evadeMetres);
      reader.Number("dodge", 0.1f, 60.0f, pilot.dodgeSeconds);
      reader.Number("flee", 0.0f, 100.0f, fleeCore);
      reader.Number("fleehull", 0.0f, 100.0f, fleeHull);
      reader.Number("gone", 0.0f, 1.0e6f, pilot.goneMetres);
    }
    if (!reader.Ok())
    {
      return false;
    }
    if (pilot.rerollMaxSeconds < pilot.rerollMinSeconds)
    {
      return reader.Fail("'rerollmax' must not be below 'rerollmin'");
    }
    pilot.fleeCorePercent = static_cast<int>(fleeCore);
    pilot.fleeHullPercent = static_cast<int>(fleeHull);
    if (content.FindPilot(pilot.name) != nullptr)
    {
      return reader.Fail("'" + pilot.name + "' is defined twice");
    }
    content.pilots.push_back(std::move(pilot));
  }
  _outContent = std::move(content);
  return true;
}

bool LoadPilots(const std::filesystem::path& _path, PilotContent& _outContent, std::string& _outError)
{
  std::string text;
  if (!Neuron::ReadTextFile(_path, text))
  {
    _outError = "cannot read " + _path.string();
    return false;
  }
  return ParsePilots(text, _path.filename().string(), _outContent, _outError);
}

} // namespace Outpost
