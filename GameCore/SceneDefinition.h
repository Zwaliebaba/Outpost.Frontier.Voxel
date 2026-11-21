#pragma once

#include "ModelAxes.h"
#include "VoxelMesher.h"
#include "WorldSnapshot.h"

#include <DirectXMath.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

// The editable scene definition, Assets/Scene.txt (ADR-002). Asset names, world placement,
// pivots, sockets, ships and lights live there and nowhere in the code.

struct ModelAsset
{
  std::string name;
  std::string file; // relative to the Assets directory
  DirectX::XMFLOAT3 pivotVox{0.0f, 0.0f, 0.0f};
  bool pivotAuthored = false; // otherwise the pivot is the centre of the model's grid
  float voxelSize = 1.0f;     // world units per voxel
  Neuron::Int3 size{0, 0, 0}; // filled from the .vox file by SetSize()
  std::vector<Neuron::Socket> sockets;

  void SetSize(const Neuron::Int3& _size) noexcept;
  [[nodiscard]] const Neuron::Socket* FindSocket(std::string_view _name) const noexcept;
};

enum class InstanceStyle : std::uint8_t
{
  Solid,
  Blueprint // drawn as a depth-tested wireframe of the same model geometry
};

struct SceneInstance
{
  std::string name;
  std::string model;
  DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 rotationDegrees{0.0f, 0.0f, 0.0f}; // yaw, pitch, roll
  float scale = 1.0f;
  std::string parent;       // attach to this instance ...
  std::string parentSocket; // ... at this socket of its model ...
  std::string socket;       // ... by this socket of our model (position/rotation are then ignored)
  InstanceStyle style = InstanceStyle::Solid;
};

struct EngineMount
{
  std::string instance;
  DirectX::XMFLOAT3 positionVox{0.0f, 0.0f, 0.0f}; // nozzle centre, VOX coordinates of the model
  float radius = 2.0f;                             // world units
  float length = 12.0f;                            // plume length, world units
};

struct PointLightDef
{
  std::string instance; // empty: position is in world space; otherwise VOX coordinates of its model
  DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 color{1.0f, 0.7f, 0.4f};
  float intensity = 1.0f;
  float radius = 30.0f;
};

struct ShipDefinition
{
  std::string instance;
  std::string callsign;
  std::string shipClass;
  int hullPercent = 100;
  std::vector<ShipModuleState> modules;
};

struct CameraDefinition
{
  DirectX::XMFLOAT3 target{0.0f, 0.0f, 0.0f};
  float yawDegrees = 0.0f;
  float pitchDegrees = 30.0f;
  float distance = 400.0f;
  float fovYDegrees = 40.0f;
};

struct KeyLightDefinition
{
  DirectX::XMFLOAT3 directionToLight{-0.5f, 0.8f, -0.3f};
  DirectX::XMFLOAT3 color{0.78f, 0.86f, 1.0f};
  float intensity = 2.0f;
  DirectX::XMFLOAT3 ambientSky{0.10f, 0.12f, 0.17f};
  DirectX::XMFLOAT3 ambientGround{0.05f, 0.045f, 0.045f};
};

struct StarFieldDefinition
{
  std::uint32_t seed = 1;
  int count = 600;
};

struct BuildItemDefinition
{
  std::string model;
  std::string label;
};

struct ResourceDefinition
{
  std::string name;
  std::int64_t amount = 0;
  std::int64_t capacity = 0;
  std::int64_t perMinute = 0; // income the server adds, spread evenly over the minute's ticks
};

struct SceneDefinition
{
  CameraDefinition camera;
  KeyLightDefinition keyLight;
  StarFieldDefinition stars;
  std::vector<ModelAsset> models;
  std::vector<SceneInstance> instances;
  std::vector<ShipDefinition> ships;
  std::vector<EngineMount> engines;
  std::vector<PointLightDef> lights;
  std::vector<BuildItemDefinition> buildItems;
  std::vector<ObjectiveState> objectives;
  std::vector<ResourceDefinition> resources;
  std::string selectedShip; // instance name

  [[nodiscard]] const ModelAsset* FindModel(std::string_view _name) const noexcept;
  [[nodiscard]] ModelAsset* FindModel(std::string_view _name) noexcept;
  [[nodiscard]] const SceneInstance* FindInstance(std::string_view _name) const noexcept;
  [[nodiscard]] int InstanceIndex(std::string_view _name) const noexcept;
  [[nodiscard]] const ShipDefinition* FindShip(std::string_view _instance) const noexcept;
};

[[nodiscard]] bool ParseScene(std::string_view _text, SceneDefinition& _outScene, std::string& _outError);
[[nodiscard]] bool LoadScene(const std::filesystem::path& _path, SceneDefinition& _outScene, std::string& _outError);

/// Every reference in the scene: model files that must exist under _assetRoot, models, parents
/// and sockets that must resolve, and ships, engines and lights that must name an instance.
/// Returns one message per problem; empty means the scene is consistent.
[[nodiscard]] std::vector<std::string> ValidateScene(const SceneDefinition& _scene, const std::filesystem::path& _assetRoot);

/// Model space to world for an instance, following socket attachments.
[[nodiscard]] DirectX::XMMATRIX ModelToWorld(const SceneDefinition& _scene, std::size_t _instanceIndex) noexcept;

/// VOX lattice coordinates to world for an instance: VoxToModelMatrix(pivot, voxelSize) * ModelToWorld.
[[nodiscard]] DirectX::XMMATRIX VoxToWorld(const SceneDefinition& _scene, std::size_t _instanceIndex) noexcept;

/// A socket's frame in world space for an instance (rows: right, up, outward normal, origin).
[[nodiscard]] DirectX::XMMATRIX SocketWorld(const SceneDefinition& _scene, std::size_t _instanceIndex,
                                            const Neuron::Socket& _socket) noexcept;

} // namespace Outpost
