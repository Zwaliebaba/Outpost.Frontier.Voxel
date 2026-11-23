#pragma once

#include "DockingPoint.h"
#include "Launcher.h"
#include "Shield.h"

#include <DirectXMath.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

struct SceneDefinition;
struct HullContent;

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
  std::string shield;                      // its shield generator, or empty for none (plan task 3.5)
  DockKind dockingClass = DockKind::Berth; // the docking points it may use (ch. 04 §2)
};

/// A launcher fitted to a hull, in the weapon module it sits in (plan task 3.4). A hull's mounts are
/// numbered in the order the file gives them.
struct MountRecord
{
  std::string ship;     // the ship archetype
  std::string launcher; // the launcher archetype
  std::string region;   // the weapon module of the ship's hull it sits in (Assets/Hulls/Hulls.txt)
  // Its muzzle, from the pivot in the ship's frame: the middle of the region's forward face. Set by
  // ResolveMounts, which needs the scene and the hulls.
  DirectX::XMFLOAT3 muzzleModel{};
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
  std::vector<ShieldArchetype> shields;
  std::vector<LauncherArchetype> launchers;
  std::vector<MountRecord> mounts; // in file order

  [[nodiscard]] const EngineArchetype* FindEngine(std::string_view _name) const noexcept;
  [[nodiscard]] const PowerPlantArchetype* FindPowerPlant(std::string_view _name) const noexcept;
  [[nodiscard]] const ThrusterArchetype* FindThruster(std::string_view _name) const noexcept;
  [[nodiscard]] const ShipArchetype* FindShip(std::string_view _name) const noexcept;
  [[nodiscard]] const ChaseCameraRecord* FindChaseCamera(std::string_view _ship) const noexcept;
  [[nodiscard]] const ShieldArchetype* FindShield(std::string_view _name) const noexcept;
  [[nodiscard]] const LauncherArchetype* FindLauncher(std::string_view _name) const noexcept;
  /// The regions a ship's mounts sit in, in mount order.
  [[nodiscard]] std::vector<std::string> MountRegions(std::string_view _ship) const;
};

/// Parses the ship family (ADR-009): unknown keywords and keys, missing keys, values out of range,
/// duplicate names and dangling engine, power plant or thruster references are errors that name
/// the file and the line.
[[nodiscard]] bool ParseShipContent(std::string_view _text, std::string_view _fileName, ShipContent& _outContent, std::string& _outError);
[[nodiscard]] bool LoadShipContent(const std::filesystem::path& _path, ShipContent& _outContent, std::string& _outError);

/// Every ship's model= names a model of the scene. Empty when all resolve; otherwise one line each.
[[nodiscard]] std::vector<std::string> ResolveShipModels(const ShipContent& _content, const SceneDefinition& _scene);

/// Every mount's region is a weapon module of its ship's hull, and no ship has more mounts than
/// MAX_LAUNCHERS. Sets each mount's muzzle from its region and the model's pivot. Empty when all
/// resolve; otherwise one line each.
[[nodiscard]] std::vector<std::string> ResolveMounts(ShipContent& _content, const SceneDefinition& _scene, const HullContent& _hulls);

} // namespace Outpost
