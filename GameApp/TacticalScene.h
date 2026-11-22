#pragma once

#include "Renderer.h"
#include "SceneAssets.h"
#include "ScreenProjection.h"
#include "WorldSnapshot.h"

#include <DirectXMath.h>

#include <array>
#include <span>
#include <string>
#include <vector>

namespace Outpost
{

/// What the view draws for one world object it has been told about: its transforms and its bounds
/// for picking and HUD anchoring, in render space (see TacticalScene), and what kind of thing it is.
/// They are placed again every frame.
struct InstanceView
{
  enum class Kind : std::uint8_t
  {
    Ship,
    Station,
    Rock,
    Debris,
    Blueprint
  };

  ObjectId object = NO_OBJECT;
  std::size_t objectIndex = 0;             // into the snapshot's objects
  std::string name;                        // the object's placement name
  const SceneInstance* instance = nullptr; // its placement record, when it has one
  const ModelAsset* asset = nullptr;
  const LoadedModel* model = nullptr;
  Kind kind = Kind::Station;
  Pose pose;                        // where it is drawn this frame, in the world
  DirectX::XMFLOAT4X4 voxToWorld{}; // to render space
  DirectX::XMFLOAT4X4 worldToVox{};
  float voxelWorldSize = 1.0f;
  Neuron::OrientedBox bounds{}; // render space, around the occupied voxels
};

/// The camera a frame is drawn from, in the form every pass needs.
struct ViewSetup
{
  DirectX::XMFLOAT4X4 view{};
  DirectX::XMFLOAT4X4 projection{};
  DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 forward{0.0f, 0.0f, 1.0f};
  DirectX::XMFLOAT2 viewportOrigin{0.0f, 0.0f};
  DirectX::XMFLOAT2 viewportSize{1.0f, 1.0f};
  float nearZ = 1.0f;
  bool orthographic = false;
};

/// The 3D half of both views: transforms and bounds for the objects the client was told about, the
/// key light's shadow camera, and the draw calls for voxels, the blueprint wireframe and the light
/// effects. It draws only what the world snapshot holds (AGENTS.md R25); the scene's assets give
/// each object its model and presentation.
///
/// **Render space** is the world moved so that a render origin near the camera is at zero (ADR-010):
/// the world is double precision and the GPU is not, so every transform here is taken relative to
/// that origin before it becomes a float. The origin is the camera's position snapped to a grid of
/// ORIGIN_CELL_METRES, so that it changes rarely and nothing near the camera is ever far from it.
class TacticalScene
{
public:
  static constexpr float EXPOSURE = 1.05f;
  static constexpr float STAR_DISTANCE = 5000.0f;
  static constexpr double ORIGIN_CELL_METRES = 1024.0;
  // The shadow camera covers what lies within this of the camera (ADR-010).
  static constexpr float SHADOW_RANGE_METRES = 1500.0f;

  /// Who exists: called when the snapshot's objects change. Places them at their poses.
  void Build(const SceneAssets& _assets, const WorldSnapshot& _world);
  /// Where everything is this frame: _poses holds one pose per snapshot object, or is empty for the
  /// snapshot's own poses. The render origin follows _camera.
  void Update(const WorldSnapshot& _world, std::span<const Pose> _poses, const WorldPosition& _camera);

  [[nodiscard]] static WorldPosition RenderOriginFor(const WorldPosition& _camera) noexcept;
  [[nodiscard]] const WorldPosition& RenderOrigin() const noexcept
  {
    return m_origin;
  }
  [[nodiscard]] DirectX::XMFLOAT3 ToRender(const WorldPosition& _position) const noexcept;
  [[nodiscard]] WorldPosition FromRender(const DirectX::XMFLOAT3& _position) const noexcept;

  [[nodiscard]] const std::vector<InstanceView>& Instances() const noexcept
  {
    return m_instances;
  }
  [[nodiscard]] const InstanceView* FindInstance(std::string_view _name) const noexcept;

  /// The nearest ship whose bounds the ray hits, or nullptr.
  [[nodiscard]] const InstanceView* PickShip(const Neuron::Ray& _ray) const noexcept;

  /// Frame constants for the scene camera: lights, shadow camera and exposure included.
  [[nodiscard]] Neuron::FrameConstants MakeFrame(const ViewSetup& _view, float _timeSeconds, bool _withPointLights) const;

  void RenderShadows(Neuron::Renderer& _renderer) const;

  /// Voxels, blueprint lines and light effects. _selected may be null.
  void Render(Neuron::Renderer& _renderer, const ViewSetup& _view, const InstanceView* _selected, float _timeSeconds) const;

  /// Draws one model alone into the current pass (thumbnails), placed with its pivot at the origin.
  static void RenderModelAlone(Neuron::Renderer& _renderer, const LoadedModel& _model, const ModelAsset& _asset, const ViewSetup& _view,
                               float _yawDegrees);

  [[nodiscard]] static Neuron::VoxelDrawConstants DrawConstants(DirectX::FXMMATRIX _voxToWorld, float _voxelWorldSize,
                                                                const ViewSetup& _view);

  /// The centre of what the shadow camera covers, in render space.
  [[nodiscard]] const DirectX::XMFLOAT3& SceneCenter() const noexcept
  {
    return m_sceneCenter;
  }

private:
  void Place(const WorldSnapshot& _world, std::span<const Pose> _poses);
  void BuildBlueprintLines();
  void BuildEffects(const WorldSnapshot& _world);
  void BuildStars();
  void BuildShadowCamera(const DirectX::XMFLOAT3& _camera);

  const SceneAssets* m_assets = nullptr;
  std::vector<InstanceView> m_instances;
  std::vector<Neuron::GpuLineSegment> m_blueprintLines;
  std::vector<Neuron::GpuEffect> m_staticGlows;
  struct Plume
  {
    DirectX::XMFLOAT3 nozzle;
    DirectX::XMFLOAT3 direction;
    float radius;
    float length;
    float phase;
    float strength; // 0 with the engine off, up past 1 with the thruster
  };
  std::vector<Plume> m_plumes;
  std::vector<DirectX::XMFLOAT4> m_starDirections; // xyz direction, w brightness
  std::vector<Neuron::GpuPointLight> m_pointLights;
  WorldPosition m_origin;
  WorldPosition m_blueprintOrigin;
  bool m_blueprintLinesValid = false;
  DirectX::XMFLOAT4X4 m_shadowViewProjection{};
  DirectX::XMFLOAT3 m_sceneCenter{0.0f, 0.0f, 0.0f};
  float m_sceneRadius = 1.0f;
};

} // namespace Outpost
