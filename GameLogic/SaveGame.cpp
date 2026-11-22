#include "SaveGame.h"

#include "SaveRecord.h"

namespace Outpost
{

void WriteUniverse(const UniverseSave& _universe, Neuron::SaveWriter& _writer)
{
  _writer.BeginSection(UNIVERSE_SECTION);
  _writer.Add("universe").Field("tick", SaveText(_universe.tick));
  for (const ResourceSave& resource : _universe.resources)
  {
    _writer.Add("resource")
      .Field("name", resource.name)
      .Field("amount", SaveText(resource.amount))
      .Field("remainder", SaveText(resource.remainder));
  }
  for (const ObjectiveSave& objective : _universe.objectives)
  {
    _writer.Add("objective").Field("text", objective.text).Field("complete", SaveText(objective.complete));
  }
}

bool ReadUniverse(const Neuron::SaveSection& _section, UniverseSave& _outUniverse, std::string& _outError)
{
  UniverseSave universe;
  bool sawUniverse = false;
  for (const Neuron::Record& record : _section.records)
  {
    SaveRecord reader(record, UNIVERSE_SECTION, _outError);
    if (record.keyword == "universe")
    {
      if (sawUniverse)
      {
        static_cast<void>(reader.Fail("a second 'universe' record"));
      }
      else if (reader.AllowOnly({"tick"}))
      {
        reader.Read("tick", universe.tick);
      }
      sawUniverse = true;
    }
    else if (record.keyword == "resource")
    {
      ResourceSave resource;
      if (reader.AllowOnly({"name", "amount", "remainder"}))
      {
        reader.Read("name", resource.name);
        reader.Read("amount", resource.amount);
        reader.Read("remainder", resource.remainder);
        universe.resources.push_back(resource);
      }
    }
    else if (record.keyword == "objective")
    {
      ObjectiveSave objective;
      if (reader.AllowOnly({"text", "complete"}))
      {
        reader.Read("text", objective.text);
        reader.Read("complete", objective.complete);
        universe.objectives.push_back(objective);
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
  if (!sawUniverse)
  {
    _outError = std::string(UNIVERSE_SECTION) + ": no 'universe' record";
    return false;
  }
  _outUniverse = std::move(universe);
  return true;
}

bool FormatSaveGame(const SaveGame& _save, std::string& _outText, std::string& _outError)
{
  Neuron::SaveWriter writer;
  WriteCharacter(_save.character, writer);
  WriteUniverse(_save.universe, writer);
  for (const Neuron::SaveSection& section : _save.kept)
  {
    writer.AddSection(section);
  }
  return Neuron::FormatSave(writer.Sections(), "A saved game of Outpost: Frontier, a text record (ADR-011)", _outText, _outError);
}

bool ParseSaveGame(std::string_view _text, SaveGame& _outSave, std::string& _outError)
{
  std::vector<Neuron::SaveSection> sections;
  if (!Neuron::ParseSave(_text, sections, _outError))
  {
    return false;
  }
  SaveGame save;
  bool sawCharacter = false;
  bool sawUniverse = false;
  for (const Neuron::SaveSection& section : sections)
  {
    if (section.name == CHARACTER_SECTION)
    {
      if (!ReadCharacter(section, save.character, _outError))
      {
        return false;
      }
      sawCharacter = true;
    }
    else if (section.name == UNIVERSE_SECTION)
    {
      if (!ReadUniverse(section, save.universe, _outError))
      {
        return false;
      }
      sawUniverse = true;
    }
    else
    {
      save.kept.push_back(section);
    }
  }
  if (!sawCharacter || !sawUniverse)
  {
    _outError = std::string("the save has no ") + std::string(!sawCharacter ? CHARACTER_SECTION : UNIVERSE_SECTION) + " section";
    return false;
  }
  _outSave = std::move(save);
  return true;
}

} // namespace Outpost
