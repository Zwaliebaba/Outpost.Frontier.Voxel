#include "SaveGame.h"

#include "SaveRecord.h"

namespace Outpost
{

void WriteWorld(const WorldSave& _world, Neuron::SaveWriter& _writer)
{
  _writer.BeginSection(WORLD_SECTION);
  _writer.Add("world").Field("tick", SaveText(_world.tick));
  for (const ResourceSave& resource : _world.resources)
  {
    _writer.Add("resource")
      .Field("name", resource.name)
      .Field("amount", SaveText(resource.amount))
      .Field("remainder", SaveText(resource.remainder));
  }
  for (const ObjectiveSave& objective : _world.objectives)
  {
    _writer.Add("objective").Field("text", objective.text).Field("complete", SaveText(objective.complete));
  }
}

bool ReadWorld(const Neuron::SaveSection& _section, WorldSave& _outWorld, std::string& _outError)
{
  WorldSave world;
  bool sawWorld = false;
  for (const Neuron::Record& record : _section.records)
  {
    SaveRecord reader(record, WORLD_SECTION, _outError);
    if (record.keyword == "world")
    {
      if (sawWorld)
      {
        static_cast<void>(reader.Fail("a second 'world' record"));
      }
      else if (reader.AllowOnly({"tick"}))
      {
        reader.Read("tick", world.tick);
      }
      sawWorld = true;
    }
    else if (record.keyword == "resource")
    {
      ResourceSave resource;
      if (reader.AllowOnly({"name", "amount", "remainder"}))
      {
        reader.Read("name", resource.name);
        reader.Read("amount", resource.amount);
        reader.Read("remainder", resource.remainder);
        world.resources.push_back(resource);
      }
    }
    else if (record.keyword == "objective")
    {
      ObjectiveSave objective;
      if (reader.AllowOnly({"text", "complete"}))
      {
        reader.Read("text", objective.text);
        reader.Read("complete", objective.complete);
        world.objectives.push_back(objective);
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
  if (!sawWorld)
  {
    _outError = std::string(WORLD_SECTION) + ": no 'world' record";
    return false;
  }
  _outWorld = std::move(world);
  return true;
}

bool FormatSaveGame(const SaveGame& _save, std::string& _outText, std::string& _outError)
{
  Neuron::SaveWriter writer;
  WriteCharacter(_save.character, writer);
  WriteWorld(_save.world, writer);
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
  bool sawWorld = false;
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
    else if (section.name == WORLD_SECTION)
    {
      if (!ReadWorld(section, save.world, _outError))
      {
        return false;
      }
      sawWorld = true;
    }
    else
    {
      save.kept.push_back(section);
    }
  }
  if (!sawCharacter || !sawWorld)
  {
    _outError = std::string("the save has no ") + std::string(!sawCharacter ? CHARACTER_SECTION : WORLD_SECTION) + " section";
    return false;
  }
  _outSave = std::move(save);
  return true;
}

} // namespace Outpost
