#include "SystemContent.h"

#include "FileSys.h"
#include "ModelAxes.h"
#include "RecordFile.h"
#include "RecordReader.h"
#include "SceneDefinition.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Outpost
{

namespace
{

constexpr float MAX_ZONE_METRES = 1.0e7f;
constexpr float MAX_POSITION_METRES = 1.0e9f;

template <typename T> [[nodiscard]] const T* FindNamed(const std::vector<T>& _items, std::string_view _name) noexcept
{
  const auto found = std::ranges::find(_items, _name, &T::name);
  return found != _items.end() ? &*found : nullptr;
}

[[nodiscard]] bool ReadZone(RecordReader& _reader, const SceneDefinition& _scene, Zone& _zone)
{
  if (!_reader.AllowOnly(
        {"name", "shape", "instance", "position", "rotation", "radius", "size", "length", "edge", "damage", "interference", "region"}))
  {
    return false;
  }
  _reader.Name(_zone.name);
  std::string shape;
  _reader.Reference("shape", shape);
  if (!_reader.Ok())
  {
    return false;
  }
  if (shape == "sphere")
  {
    _zone.shape = ZoneShape::Sphere;
    _reader.Number("radius", 0.001f, MAX_ZONE_METRES, _zone.sizeMetres.x);
  }
  else if (shape == "box")
  {
    _zone.shape = ZoneShape::Box;
    _reader.Vector("size", 0.001f, MAX_ZONE_METRES, _zone.sizeMetres);
  }
  else if (shape == "cylinder")
  {
    _zone.shape = ZoneShape::Cylinder;
    _reader.Number("radius", 0.001f, MAX_ZONE_METRES, _zone.sizeMetres.x);
    _reader.Number("length", 0.001f, MAX_ZONE_METRES, _zone.sizeMetres.y);
  }
  else
  {
    return _reader.Fail("'shape' must be sphere, box or cylinder");
  }
  const Neuron::Record& record = _reader.Source();
  const bool sizeKeysFit = shape == "box" ? record.Find("radius") == nullptr && record.Find("length") == nullptr
                                          : record.Find("size") == nullptr && (shape == "cylinder" || record.Find("length") == nullptr);
  if (_reader.Ok() && !sizeKeysFit)
  {
    return _reader.Fail("a " + shape + " is sized by " + (shape == "box" ? "size" : shape == "sphere" ? "radius" : "radius and length"));
  }
  XMFLOAT3 position{};
  XMFLOAT3 rotationDegrees{};
  _reader.Vector("position", -MAX_POSITION_METRES, MAX_POSITION_METRES, position);
  _reader.OptionalVector("rotation", -360.0f, 360.0f, rotationDegrees);
  _reader.OptionalNumber("edge", 0.0f, 1.0f, _zone.edgeFraction);
  _reader.OptionalNumber("damage", 0.0f, 1.0e7f, _zone.damagePerSecond);
  _reader.OptionalNumber("interference", 0.0f, 1.0f, _zone.interference);
  _reader.OptionalString("region", _zone.region);
  std::string instanceName;
  _reader.OptionalString("instance", instanceName);
  if (!_reader.Ok())
  {
    return false;
  }
  XMMATRIX toWorld = Neuron::InstanceMatrix(position, rotationDegrees, 1.0f);
  if (!instanceName.empty())
  {
    // Placed on a model, like a light: the position is in that instance's VOX coordinates and the
    // rotation is relative to the instance.
    const int index = _scene.InstanceIndex(instanceName);
    if (index < 0)
    {
      return _reader.Fail("zone on unknown instance '" + instanceName + "'");
    }
    const XMMATRIX voxToWorld = VoxToWorld(_scene, static_cast<std::size_t>(index));
    XMFLOAT3 worldPosition{};
    XMStoreFloat3(&worldPosition, XMVector3TransformCoord(XMLoadFloat3(&position), voxToWorld));
    const XMMATRIX instanceRotation = ModelToWorld(_scene, static_cast<std::size_t>(index));
    toWorld = Neuron::InstanceMatrix(XMFLOAT3{0.0f, 0.0f, 0.0f}, rotationDegrees, 1.0f) * instanceRotation;
    position = worldPosition;
  }
  XMMATRIX rotation = XMMatrixIdentity();
  rotation.r[0] = XMVector3Normalize(toWorld.r[0]);
  rotation.r[1] = XMVector3Normalize(toWorld.r[1]);
  rotation.r[2] = XMVector3Normalize(toWorld.r[2]);
  XMStoreFloat4(&_zone.orientation, XMQuaternionNormalize(XMQuaternionRotationMatrix(rotation)));
  _zone.position = UniversePosition{position.x, position.y, position.z};
  return true;
}

[[nodiscard]] bool HasDockingPoint(const SceneDefinition& _scene, std::string_view _instance) noexcept
{
  const SceneInstance* instance = _scene.FindInstance(_instance);
  const ModelAsset* model = instance != nullptr ? _scene.FindModel(instance->model) : nullptr;
  return model != nullptr && !model->dockingPoints.empty();
}

} // namespace

const Zone* SystemContent::FindZone(std::string_view _name) const noexcept
{
  return FindNamed(zones, _name);
}

const StationDefinition* SystemContent::FindStation(std::string_view _name) const noexcept
{
  return FindNamed(stations, _name);
}

const StationDefinition* SystemContent::StationOf(std::string_view _instance) const noexcept
{
  for (const StationDefinition& station : stations)
  {
    if (std::ranges::find(station.instances, _instance) != station.instances.end())
    {
      return &station;
    }
  }
  return nullptr;
}

bool ParseSystemContent(std::string_view _text, std::string_view _fileName, const SceneDefinition& _scene, SystemContent& _outContent,
                        std::string& _outError)
{
  std::vector<Neuron::Record> records;
  if (!Neuron::ParseRecords(_text, records, _outError))
  {
    _outError = std::string(_fileName) + ", " + _outError;
    return false;
  }
  SystemContent content;
  std::vector<int> stationLines;
  int startLine = 0;
  for (const Neuron::Record& record : records)
  {
    RecordReader reader(record, _fileName, _outError);
    if (record.keyword == "zone")
    {
      Zone zone;
      if (!ReadZone(reader, _scene, zone))
      {
        return false;
      }
      if (content.FindZone(zone.name) != nullptr)
      {
        return reader.Fail("'" + zone.name + "' is defined twice");
      }
      content.zones.push_back(std::move(zone));
    }
    else if (record.keyword == "station")
    {
      StationDefinition station;
      if (reader.AllowOnly({"name", "label", "region", "instance"}))
      {
        reader.Name(station.name);
        reader.Reference("label", station.label);
        reader.OptionalString("region", station.region);
      }
      if (!reader.Ok())
      {
        return false;
      }
      for (const std::string* instance : record.FindAll("instance"))
      {
        if (_scene.FindInstance(*instance) == nullptr)
        {
          return reader.Fail("station '" + station.name + "' names unknown instance '" + *instance + "'");
        }
        if (content.StationOf(*instance) != nullptr || std::ranges::find(station.instances, *instance) != station.instances.end())
        {
          return reader.Fail("instance '" + *instance + "' belongs to a station twice");
        }
        station.instances.push_back(*instance);
      }
      if (std::ranges::none_of(station.instances, [&_scene](const std::string& _instance) { return HasDockingPoint(_scene, _instance); }))
      {
        return reader.Fail("station '" + station.name + "' has no docking point: none of its instances' models has one");
      }
      if (content.FindStation(station.name) != nullptr)
      {
        return reader.Fail("'" + station.name + "' is defined twice");
      }
      content.stations.push_back(std::move(station));
      stationLines.push_back(record.line);
    }
    else if (record.keyword == "start")
    {
      if (reader.AllowOnly({"station"}))
      {
        reader.Reference("station", content.startStation);
      }
      if (!reader.Ok())
      {
        return false;
      }
      if (startLine != 0)
      {
        return reader.Fail("a second 'start' record");
      }
      startLine = record.line;
    }
    else
    {
      return reader.Fail("unknown keyword '" + record.keyword + "'");
    }
  }
  const auto lineError = [&_fileName](int _line, const std::string& _message)
  { return std::string(_fileName) + " line " + std::to_string(_line) + ": " + _message; };
  for (std::size_t i = 0; i < content.stations.size(); ++i)
  {
    const StationDefinition& station = content.stations[i];
    if (!station.region.empty())
    {
      const Zone* region = content.FindZone(station.region);
      if (region == nullptr || region->region.empty())
      {
        _outError =
          lineError(stationLines[i], "station '" + station.name + "' names '" + station.region + "', which is not a named region");
        return false;
      }
    }
  }
  if (startLine == 0)
  {
    _outError = std::string(_fileName) + ": no 'start' record says where a new game starts";
    return false;
  }
  if (content.FindStation(content.startStation) == nullptr)
  {
    _outError = lineError(startLine, "unknown station '" + content.startStation + "'");
    return false;
  }
  _outContent = std::move(content);
  return true;
}

bool LoadSystemContent(const std::filesystem::path& _path, const SceneDefinition& _scene, SystemContent& _outContent,
                       std::string& _outError)
{
  std::string text;
  if (!Neuron::ReadTextFile(_path, text))
  {
    _outError = "cannot read " + _path.string();
    return false;
  }
  return ParseSystemContent(text, _path.filename().string(), _scene, _outContent, _outError);
}

} // namespace Outpost
