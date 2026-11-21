#include "SceneDefinition.h"

#include "FileSys.h"
#include "RecordFile.h"

#include <system_error>

using namespace DirectX;

namespace Outpost
{

namespace
{

constexpr int MAX_ATTACHMENT_DEPTH = 16;

[[nodiscard]] std::string LineError(const Neuron::Record& _record, std::string_view _message)
{
  return "Scene.txt line " + std::to_string(_record.line) + " (" + _record.keyword + "): " + std::string(_message);
}

// Reads an optional field; a field that is present but malformed is an error.
template <typename T, typename Getter>
[[nodiscard]] bool Optional(const Neuron::Record& _record, std::string_view _key, T& _value, Getter _getter, std::string& _error)
{
  if (_record.Find(_key) == nullptr)
  {
    return true;
  }
  if (!(_record.*_getter)(_key, _value))
  {
    _error = LineError(_record, "malformed '" + std::string(_key) + "'");
    return false;
  }
  return true;
}

[[nodiscard]] bool Required(const Neuron::Record& _record, std::string_view _key, std::string& _value, std::string& _error)
{
  if (!_record.GetString(_key, _value) || _value.empty())
  {
    _error = LineError(_record, "missing '" + std::string(_key) + "'");
    return false;
  }
  return true;
}

// "Hull Section (B):34" -> {"Hull Section (B)", 34}
[[nodiscard]] bool ParseModule(std::string_view _text, ShipModuleState& _out)
{
  const std::size_t colon = _text.rfind(':');
  if (colon == std::string_view::npos || colon == 0)
  {
    return false;
  }
  int health = 0;
  if (!Neuron::ParseInt(_text.substr(colon + 1), health) || health < 0 || health > 100)
  {
    return false;
  }
  _out.name = std::string(_text.substr(0, colon));
  _out.healthPercent = health;
  return true;
}

[[nodiscard]] XMMATRIX ModelToWorldRecursive(const SceneDefinition& _scene, std::size_t _index, int _depth) noexcept
{
  const SceneInstance& instance = _scene.instances[_index];
  if (!instance.parent.empty() && _depth < MAX_ATTACHMENT_DEPTH)
  {
    const int parentIndex = _scene.InstanceIndex(instance.parent);
    const ModelAsset* model = _scene.FindModel(instance.model);
    if (parentIndex >= 0 && model != nullptr)
    {
      const SceneInstance& parent = _scene.instances[static_cast<std::size_t>(parentIndex)];
      const ModelAsset* parentModel = _scene.FindModel(parent.model);
      const Neuron::Socket* parentSocket = parentModel != nullptr ? parentModel->FindSocket(instance.parentSocket) : nullptr;
      const Neuron::Socket* childSocket = model->FindSocket(instance.socket);
      if (parentSocket != nullptr && childSocket != nullptr)
      {
        const XMMATRIX parentSocketWorld = Neuron::SocketFrame(*parentSocket, parentModel->pivotVox, parentModel->voxelSize) *
                                           ModelToWorldRecursive(_scene, static_cast<std::size_t>(parentIndex), _depth + 1);
        return Neuron::MateSockets(Neuron::SocketFrame(*childSocket, model->pivotVox, model->voxelSize), parentSocketWorld);
      }
    }
  }
  return Neuron::InstanceMatrix(instance.position, instance.rotationDegrees, instance.scale);
}

} // namespace

void ModelAsset::SetSize(const Neuron::Int3& _size) noexcept
{
  size = _size;
  if (!pivotAuthored)
  {
    pivotVox = XMFLOAT3{static_cast<float>(_size.x) * 0.5f, static_cast<float>(_size.y) * 0.5f, static_cast<float>(_size.z) * 0.5f};
  }
}

const Neuron::Socket* ModelAsset::FindSocket(std::string_view _name) const noexcept
{
  for (const Neuron::Socket& socket : sockets)
  {
    if (socket.name == _name)
    {
      return &socket;
    }
  }
  return nullptr;
}

const ModelAsset* SceneDefinition::FindModel(std::string_view _name) const noexcept
{
  for (const ModelAsset& model : models)
  {
    if (model.name == _name)
    {
      return &model;
    }
  }
  return nullptr;
}

ModelAsset* SceneDefinition::FindModel(std::string_view _name) noexcept
{
  for (ModelAsset& model : models)
  {
    if (model.name == _name)
    {
      return &model;
    }
  }
  return nullptr;
}

const SceneInstance* SceneDefinition::FindInstance(std::string_view _name) const noexcept
{
  const int index = InstanceIndex(_name);
  return index >= 0 ? &instances[static_cast<std::size_t>(index)] : nullptr;
}

int SceneDefinition::InstanceIndex(std::string_view _name) const noexcept
{
  for (std::size_t i = 0; i < instances.size(); ++i)
  {
    if (instances[i].name == _name)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

const ShipDefinition* SceneDefinition::FindShip(std::string_view _instance) const noexcept
{
  for (const ShipDefinition& ship : ships)
  {
    if (ship.instance == _instance)
    {
      return &ship;
    }
  }
  return nullptr;
}

bool ParseScene(std::string_view _text, SceneDefinition& _outScene, std::string& _outError)
{
  _outScene = SceneDefinition{};
  std::vector<Neuron::Record> records;
  if (!Neuron::ParseRecords(_text, records, _outError))
  {
    _outError = "Scene.txt " + _outError;
    return false;
  }

  for (const Neuron::Record& record : records)
  {
    std::string& error = _outError;
    if (record.keyword == "camera")
    {
      CameraDefinition& camera = _outScene.camera;
      if (!Optional(record, "target", camera.target, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "yaw", camera.yawDegrees, &Neuron::Record::GetFloat, error) ||
          !Optional(record, "pitch", camera.pitchDegrees, &Neuron::Record::GetFloat, error) ||
          !Optional(record, "distance", camera.distance, &Neuron::Record::GetFloat, error) ||
          !Optional(record, "fov", camera.fovYDegrees, &Neuron::Record::GetFloat, error))
      {
        return false;
      }
    }
    else if (record.keyword == "keylight")
    {
      KeyLightDefinition& light = _outScene.keyLight;
      if (!Optional(record, "direction", light.directionToLight, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "color", light.color, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "intensity", light.intensity, &Neuron::Record::GetFloat, error) ||
          !Optional(record, "ambientsky", light.ambientSky, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "ambientground", light.ambientGround, &Neuron::Record::GetFloat3, error))
      {
        return false;
      }
    }
    else if (record.keyword == "stars")
    {
      int seed = static_cast<int>(_outScene.stars.seed);
      if (!Optional(record, "seed", seed, &Neuron::Record::GetInt, error) ||
          !Optional(record, "count", _outScene.stars.count, &Neuron::Record::GetInt, error))
      {
        return false;
      }
      _outScene.stars.seed = static_cast<std::uint32_t>(seed);
    }
    else if (record.keyword == "model")
    {
      ModelAsset model;
      if (!Required(record, "name", model.name, error) || !Required(record, "file", model.file, error) ||
          !Optional(record, "voxelsize", model.voxelSize, &Neuron::Record::GetFloat, error) ||
          !Optional(record, "pivot", model.pivotVox, &Neuron::Record::GetFloat3, error))
      {
        return false;
      }
      model.pivotAuthored = record.Find("pivot") != nullptr;
      if (_outScene.FindModel(model.name) != nullptr)
      {
        _outError = LineError(record, "duplicate model '" + model.name + "'");
        return false;
      }
      _outScene.models.push_back(std::move(model));
    }
    else if (record.keyword == "socket")
    {
      std::string modelName;
      Neuron::Socket socket{{}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
      if (!Required(record, "model", modelName, error) || !Required(record, "name", socket.name, error) ||
          !Optional(record, "position", socket.positionVox, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "normal", socket.normalVox, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "up", socket.upVox, &Neuron::Record::GetFloat3, error))
      {
        return false;
      }
      ModelAsset* model = _outScene.FindModel(modelName);
      if (model == nullptr)
      {
        _outError = LineError(record, "socket on unknown model '" + modelName + "'");
        return false;
      }
      model->sockets.push_back(std::move(socket));
    }
    else if (record.keyword == "instance")
    {
      SceneInstance instance;
      std::string style = "solid";
      if (!Required(record, "name", instance.name, error) || !Required(record, "model", instance.model, error) ||
          !Optional(record, "position", instance.position, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "rotation", instance.rotationDegrees, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "scale", instance.scale, &Neuron::Record::GetFloat, error) ||
          !Optional(record, "parent", instance.parent, &Neuron::Record::GetString, error) ||
          !Optional(record, "parentsocket", instance.parentSocket, &Neuron::Record::GetString, error) ||
          !Optional(record, "socket", instance.socket, &Neuron::Record::GetString, error) ||
          !Optional(record, "style", style, &Neuron::Record::GetString, error))
      {
        return false;
      }
      if (style == "blueprint")
      {
        instance.style = InstanceStyle::Blueprint;
      }
      else if (style != "solid")
      {
        _outError = LineError(record, "style must be solid or blueprint");
        return false;
      }
      if (_outScene.InstanceIndex(instance.name) >= 0)
      {
        _outError = LineError(record, "duplicate instance '" + instance.name + "'");
        return false;
      }
      _outScene.instances.push_back(std::move(instance));
    }
    else if (record.keyword == "ship")
    {
      ShipDefinition ship;
      if (!Required(record, "instance", ship.instance, error) || !Required(record, "callsign", ship.callsign, error) ||
          !Required(record, "class", ship.shipClass, error) || !Optional(record, "hull", ship.hullPercent, &Neuron::Record::GetInt, error))
      {
        return false;
      }
      for (const std::string* text : record.FindAll("module"))
      {
        ShipModuleState module;
        if (!ParseModule(*text, module))
        {
          _outError = LineError(record, "module must be \"Name:percent\", found '" + *text + "'");
          return false;
        }
        ship.modules.push_back(std::move(module));
      }
      _outScene.ships.push_back(std::move(ship));
    }
    else if (record.keyword == "engine")
    {
      EngineMount engine;
      if (!Required(record, "instance", engine.instance, error) ||
          !Optional(record, "position", engine.positionVox, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "radius", engine.radius, &Neuron::Record::GetFloat, error) ||
          !Optional(record, "length", engine.length, &Neuron::Record::GetFloat, error))
      {
        return false;
      }
      _outScene.engines.push_back(std::move(engine));
    }
    else if (record.keyword == "light")
    {
      PointLightDef light;
      if (!Optional(record, "instance", light.instance, &Neuron::Record::GetString, error) ||
          !Optional(record, "position", light.position, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "color", light.color, &Neuron::Record::GetFloat3, error) ||
          !Optional(record, "intensity", light.intensity, &Neuron::Record::GetFloat, error) ||
          !Optional(record, "radius", light.radius, &Neuron::Record::GetFloat, error))
      {
        return false;
      }
      _outScene.lights.push_back(std::move(light));
    }
    else if (record.keyword == "build")
    {
      BuildItemDefinition item;
      if (!Required(record, "model", item.model, error) || !Required(record, "label", item.label, error))
      {
        return false;
      }
      _outScene.buildItems.push_back(std::move(item));
    }
    else if (record.keyword == "objective")
    {
      ObjectiveState objective;
      if (!Required(record, "text", objective.text, error) ||
          !Optional(record, "done", objective.complete, &Neuron::Record::GetBool, error))
      {
        return false;
      }
      _outScene.objectives.push_back(std::move(objective));
    }
    else if (record.keyword == "resource")
    {
      ResourceDefinition resource;
      int amount = 0;
      int capacity = 0;
      int perMinute = 0;
      if (!Required(record, "name", resource.name, error) || !Optional(record, "amount", amount, &Neuron::Record::GetInt, error) ||
          !Optional(record, "capacity", capacity, &Neuron::Record::GetInt, error) ||
          !Optional(record, "perminute", perMinute, &Neuron::Record::GetInt, error))
      {
        return false;
      }
      resource.amount = amount;
      resource.capacity = capacity;
      resource.perMinute = perMinute;
      _outScene.resources.push_back(std::move(resource));
    }
    else if (record.keyword == "select")
    {
      if (!Required(record, "instance", _outScene.selectedShip, error))
      {
        return false;
      }
    }
    else
    {
      _outError = LineError(record, "unknown keyword");
      return false;
    }
  }
  return true;
}

bool LoadScene(const std::filesystem::path& _path, SceneDefinition& _outScene, std::string& _outError)
{
  std::string text;
  if (!Neuron::ReadTextFile(_path, text))
  {
    _outError = "cannot read " + _path.string();
    return false;
  }
  return ParseScene(text, _outScene, _outError);
}

std::vector<std::string> ValidateScene(const SceneDefinition& _scene, const std::filesystem::path& _assetRoot)
{
  std::vector<std::string> problems;
  std::error_code error;
  for (const ModelAsset& model : _scene.models)
  {
    if (!std::filesystem::exists(_assetRoot / model.file, error))
    {
      problems.push_back("model '" + model.name + "' file not found: " + model.file);
    }
  }
  for (const SceneInstance& instance : _scene.instances)
  {
    const ModelAsset* model = _scene.FindModel(instance.model);
    if (model == nullptr)
    {
      problems.push_back("instance '" + instance.name + "' uses unknown model '" + instance.model + "'");
      continue;
    }
    if (instance.parent.empty())
    {
      continue;
    }
    const SceneInstance* parent = _scene.FindInstance(instance.parent);
    if (parent == nullptr)
    {
      problems.push_back("instance '" + instance.name + "' attaches to unknown instance '" + instance.parent + "'");
      continue;
    }
    const ModelAsset* parentModel = _scene.FindModel(parent->model);
    if (parentModel == nullptr || parentModel->FindSocket(instance.parentSocket) == nullptr)
    {
      problems.push_back("instance '" + instance.name + "' attaches to missing socket '" + instance.parentSocket + "'");
    }
    if (model->FindSocket(instance.socket) == nullptr)
    {
      problems.push_back("instance '" + instance.name + "' has no socket '" + instance.socket + "'");
    }
  }
  for (const ShipDefinition& ship : _scene.ships)
  {
    if (_scene.FindInstance(ship.instance) == nullptr)
    {
      problems.push_back("ship '" + ship.callsign + "' names unknown instance '" + ship.instance + "'");
    }
  }
  for (const EngineMount& engine : _scene.engines)
  {
    if (_scene.FindInstance(engine.instance) == nullptr)
    {
      problems.push_back("engine names unknown instance '" + engine.instance + "'");
    }
  }
  for (const PointLightDef& light : _scene.lights)
  {
    if (!light.instance.empty() && _scene.FindInstance(light.instance) == nullptr)
    {
      problems.push_back("light names unknown instance '" + light.instance + "'");
    }
  }
  for (const BuildItemDefinition& item : _scene.buildItems)
  {
    if (_scene.FindModel(item.model) == nullptr)
    {
      problems.push_back("build item '" + item.label + "' uses unknown model '" + item.model + "'");
    }
  }
  if (!_scene.selectedShip.empty() && _scene.FindShip(_scene.selectedShip) == nullptr)
  {
    problems.push_back("selected ship '" + _scene.selectedShip + "' is not a ship");
  }
  return problems;
}

XMMATRIX ModelToWorld(const SceneDefinition& _scene, std::size_t _instanceIndex) noexcept
{
  return ModelToWorldRecursive(_scene, _instanceIndex, 0);
}

XMMATRIX VoxToWorld(const SceneDefinition& _scene, std::size_t _instanceIndex) noexcept
{
  const ModelAsset* model = _scene.FindModel(_scene.instances[_instanceIndex].model);
  const XMFLOAT3 pivot = model != nullptr ? model->pivotVox : XMFLOAT3{0.0f, 0.0f, 0.0f};
  const float voxelSize = model != nullptr ? model->voxelSize : 1.0f;
  return Neuron::VoxToModelMatrix(pivot, voxelSize) * ModelToWorld(_scene, _instanceIndex);
}

XMMATRIX SocketWorld(const SceneDefinition& _scene, std::size_t _instanceIndex, const Neuron::Socket& _socket) noexcept
{
  const ModelAsset* model = _scene.FindModel(_scene.instances[_instanceIndex].model);
  const XMFLOAT3 pivot = model != nullptr ? model->pivotVox : XMFLOAT3{0.0f, 0.0f, 0.0f};
  const float voxelSize = model != nullptr ? model->voxelSize : 1.0f;
  return Neuron::SocketFrame(_socket, pivot, voxelSize) * ModelToWorld(_scene, _instanceIndex);
}

} // namespace Outpost
