#pragma once

#include "Renderer.h"
#include "SceneAssets.h"
#include "ScreenProjection.h"
#include "WorldSnapshot.h"

#include <DirectXMath.h>

#include <array>
#include <string>
#include <vector>

namespace Outpost
{

/// What the tactical view draws for one world object it has been told about: its transforms, its
/// world bounds for picking and HUD anchoring, and what kind of thing it is.
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
  std::string name;                        // the object's placement name
  const SceneInstance* instance = nullptr; // its placement record, when it has one
  const ModelAsset* asset = nullptr;
  const LoadedModel* model = nullptr;
  Kind kind = Kind::Station;
  DirectX::XMFLOAT4X4 voxToWorld{};
  DirectX::XMFLOAT4X4 worldToVox{};
  float voxelWorldSize = 1.0f;
  Neuron::OrientedBox bounds{}; // world space, around the occupied voxels
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

/// The 3D half of the tactical view: transforms and bounds for the objects the client was told
/// about, the key light's shadow camera, and the draw calls for voxels, the blueprint wireframe and
/// the light effects. It draws only what the world snapshot holds (AGENTS.md R25), placed at the
/// poses the server sent; the scene's assets give each object its model and presentation.
class TacticalScene
{
public:
  static constexpr float EXPOSURE = 1.05f;
  static constexpr float STAR_DISTANCE = 5000.0f;

  void Build(const SceneAssets& _assets, const WorldSnapshot& _world);

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

  [[nodiscard]] const DirectX::XMFLOAT3& SceneCenter() const noexcept
  {
    return m_sceneCenter;
  }

private:
  void BuildEffects();
  void BuildShadowCamera();

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
  };
  std::vector<Plume> m_plumes;
  std::vector<DirectX::XMFLOAT4> m_starDirections; // xyz direction, w brightness
  std::vector<Neuron::GpuPointLight> m_pointLights;
  DirectX::XMFLOAT4X4 m_shadowViewProjection{};
  DirectX::XMFLOAT3 m_sceneCenter{0.0f, 0.0f, 0.0f};
  float m_sceneRadius = 1.0f;
};

} // namespace Outpost
