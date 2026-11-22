#pragma once

#include <DirectXMath.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

struct SceneDefinition;

/// An engine as authored (blueprint ch. 02 §4). Almost all of a ship's drag is its engine's, so
/// killing the engine is what lets a ship drift.
struct EngineArchetype
{
  std::string name;
  float thrustNewtons = 0.0f;
  float dragKilogramsPerSecond = 0.0f;
  float reverseFraction = 0.0f; // of thrust, for a negative throttle
  float cruiseChargeSeconds = 0.0f;
  float cruisePowerPerSecond = 0.0f; // drawn from the power pool while cruise is engaged
};

/// Fills the two pools each tick: power for weapons, shields and cruise; thrust for the thruster
/// and strafing (blueprint ch. 02 §3.5).
struct PowerPlantArchetype
{
  std::string name;
  float powerCapacity = 0.0f;
  float powerChargePerSecond = 0.0f;
  float thrustCapacity = 0.0f;
  float thrustChargePerSecond = 0.0f;
};

struct ThrusterArchetype
{
  std::string name;
  float forceNewtons = 0.0f;
  float drainPerSecond = 0.0f; // from the thrust pool
};

/// A hull and the equipment it flies with. Per-axis vectors are pitch (X), yaw (Y), roll (Z) in
/// the ship's frame, which faces +Z with +Y up, as the ship models do (ADR-002). In that
/// left-handed frame a positive yaw turns the nose right, a positive pitch turns it down, and a
/// positive roll lifts the right wing.
struct ShipArchetype
{
  std::string name;
  std::string model;
  float massKilograms = 0.0f;
  DirectX::XMFLOAT3 inertiaKilogramSquareMetres{};
  float hullDragKilogramsPerSecond = 0.0f;
  DirectX::XMFLOAT3 steeringTorqueNewtonMetres{};
  DirectX::XMFLOAT3 angularDragNewtonMetreSeconds{};
  float strafeForceNewtons = 0.0f;
  float strafeDrainPerSecond = 0.0f;
  float maxBankDegrees = 0.0f;
  std::string engine;
  std::string powerPlant;
  std::string thruster;
};

/// How the chase camera sits behind one hull (blueprint ch. 09 §3.9). Presentation, so a record of
/// its own beside the hull's rather than fields of it: the server never reads one.
struct ChaseCameraRecord
{
  std::string ship; // the ship archetype it frames
  float upMetres = 0.0f;
  float backMetres = 0.0f;
  float lookSideDegrees = 0.0f; // turn look-ahead at full yaw throttle
  float lookUpDegrees = 0.0f;   // ... at full pitch-up throttle
  float lookDownDegrees = 0.0f; // ... at full pitch-down throttle
};

/// Assets/Content/Ships.txt as loaded: every record, every reference inside the file resolved.
struct ShipContent
{
  std::vector<EngineArchetype> engines;
  std::vector<PowerPlantArchetype> powerPlants;
  std::vector<ThrusterArchetype> thrusters;
  std::vector<ShipArchetype> ships;
  std::vector<ChaseCameraRecord> chaseCameras;

  [[nodiscard]] const EngineArchetype* FindEngine(std::string_view _name) const noexcept;
  [[nodiscard]] const PowerPlantArchetype* FindPowerPlant(std::string_view _name) const noexcept;
  [[nodiscard]] const ThrusterArchetype* FindThruster(std::string_view _name) const noexcept;
  [[nodiscard]] const ShipArchetype* FindShip(std::string_view _name) const noexcept;
  [[nodiscard]] const ChaseCameraRecord* FindChaseCamera(std::string_view _ship) const noexcept;
};

/// Parses the ship family (ADR-009): unknown keywords and keys, missing keys, values out of range,
/// duplicate names and dangling engine, power plant or thruster references are errors that name
/// the file and the line.
[[nodiscard]] bool ParseShipContent(std::string_view _text, std::string_view _fileName, ShipContent& _outContent, std::string& _outError);
[[nodiscard]] bool LoadShipContent(const std::filesystem::path& _path, ShipContent& _outContent, std::string& _outError);

/// Every ship's model= names a model of the scene. Empty when all resolve; otherwise one line each.
[[nodiscard]] std::vector<std::string> ResolveShipModels(const ShipContent& _content, const SceneDefinition& _scene);

} // namespace Outpost
