#include "TacticalScene.h"

#include "ModelAxes.h"
#include "Random.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace Outpost
{

using namespace DirectX;

namespace
{

constexpr XMFLOAT4 BLUEPRINT_COLOR{0.22f, 0.86f, 1.0f, 0.85f};
constexpr XMFLOAT4 BLUEPRINT_GLOW{0.10f, 0.55f, 1.0f, 0.07f};
constexpr XMFLOAT3 SELECTION_RIM{0.25f, 0.85f, 1.0f};
// A flying ship's plume at zero throttle, as a share of its full length.
constexpr float PLUME_IDLE = 0.2f;

[[nodiscard]] XMFLOAT3 TransformPoint(const XMFLOAT3& _point, FXMMATRIX _matrix) noexcept
{
  XMFLOAT3 result{};
  XMStoreFloat3(&result, XMVector3TransformCoord(XMLoadFloat3(&_point), _matrix));
  return result;
}

[[nodiscard]] XMFLOAT3 ToFloat3(const Neuron::Int3& _point) noexcept
{
  return XMFLOAT3{static_cast<float>(_point.x), static_cast<float>(_point.y), static_cast<float>(_point.z)};
}

[[nodiscard]] bool StartsWith(std::string_view _text, std::string_view _prefix) noexcept
{
  return _text.substr(0, _prefix.size()) == _prefix;
}

} // namespace

void TacticalScene::Build(const SceneAssets& _assets, const UniverseSnapshot& _universe)
{
  m_assets = &_assets;
  m_instances.clear();
  m_blueprintLinesValid = false;
  const SceneDefinition& scene = _assets.Scene();
  for (std::size_t i = 0; i < _universe.objects.size(); ++i)
  {
    const UniverseObject& object = _universe.objects[i];
    InstanceView view;
    view.object = object.id;
    view.objectIndex = i;
    view.name = object.name;
    view.instance = scene.FindInstance(object.name);
    view.asset = scene.FindModel(object.model);
    view.model = _assets.FindModel(object.model);
    view.sharedModel = view.model;
    if (view.asset == nullptr || view.model == nullptr)
    {
      continue;
    }
    bool isShip = false;
    for (const ShipState& ship : _universe.ships)
    {
      isShip = isShip || ship.object == object.id;
    }
    if (view.instance != nullptr && view.instance->style == InstanceStyle::Blueprint)
    {
      view.kind = InstanceView::Kind::Blueprint;
    }
    else if (isShip)
    {
      view.kind = InstanceView::Kind::Ship;
    }
    else if (StartsWith(object.model, "Asteroid"))
    {
      view.kind = InstanceView::Kind::Rock;
    }
    else if (StartsWith(object.model, "Debris"))
    {
      view.kind = InstanceView::Kind::Debris;
    }
    if (view.instance == nullptr)
    {
      // Only a fight makes objects the scene did not place: what it cut loose.
      view.kind = InstanceView::Kind::Wreck;
    }
    m_instances.push_back(view);
  }
  BuildStars();
  // Placed at the snapshot's poses until the first Update says where the camera is.
  Update(_universe, {}, m_origin);
}

void TacticalScene::SetOverrides(std::vector<ModelOverride> _overrides)
{
  m_overrides = std::move(_overrides);
  std::ranges::sort(m_overrides, {}, &ModelOverride::object);
}

UniversePosition TacticalScene::RenderOriginFor(const UniversePosition& _camera) noexcept
{
  const auto snap = [](double _value) { return std::floor(_value / ORIGIN_CELL_METRES + 0.5) * ORIGIN_CELL_METRES; };
  return UniversePosition{snap(_camera.x), snap(_camera.y), snap(_camera.z)};
}

XMFLOAT3 TacticalScene::ToRender(const UniversePosition& _position) const noexcept
{
  return XMFLOAT3{static_cast<float>(_position.x - m_origin.x), static_cast<float>(_position.y - m_origin.y),
                  static_cast<float>(_position.z - m_origin.z)};
}

UniversePosition TacticalScene::FromRender(const XMFLOAT3& _position) const noexcept
{
  return UniversePosition{m_origin.x + static_cast<double>(_position.x), m_origin.y + static_cast<double>(_position.y),
                          m_origin.z + static_cast<double>(_position.z)};
}

void TacticalScene::Update(const UniverseSnapshot& _universe, std::span<const Pose> _poses, const UniversePosition& _camera)
{
  if (m_assets == nullptr)
  {
    return;
  }
  m_origin = RenderOriginFor(_camera);
  Place(_universe, _poses);
  if (!m_blueprintLinesValid || !(m_blueprintOrigin == m_origin))
  {
    BuildBlueprintLines();
  }
  BuildEffects(_universe);
  BuildShadowCamera(ToRender(_camera));
}

void TacticalScene::Place(const UniverseSnapshot& _universe, std::span<const Pose> _poses)
{
  for (InstanceView& view : m_instances)
  {
    if (view.objectIndex >= _universe.objects.size())
    {
      continue;
    }
    UniverseObject object = _universe.objects[view.objectIndex];
    if (view.objectIndex < _poses.size())
    {
      object.pose = _poses[view.objectIndex];
    }
    view.pose = object.pose;
    // A damaged object is drawn from its own surface; a wreck, only once it has one.
    const auto overridden = std::ranges::lower_bound(m_overrides, view.object, {}, &ModelOverride::object);
    const bool found = overridden != m_overrides.end() && overridden->object == view.object;
    view.model = found && overridden->model != nullptr ? overridden->model : view.sharedModel;
    view.hidden = (found && overridden->model == nullptr) || (view.kind == InstanceView::Kind::Wreck && !found);
    // The pose relative to the render origin, so that the float matrix stays precise (ADR-010).
    object.pose.position =
      UniversePosition{object.pose.position.x - m_origin.x, object.pose.position.y - m_origin.y, object.pose.position.z - m_origin.z};
    const XMMATRIX voxToWorld = Neuron::VoxToModelMatrix(view.asset->pivotVox, view.asset->voxelSize) * ObjectToWorld(object);
    XMStoreFloat4x4(&view.voxToWorld, voxToWorld);
    XMStoreFloat4x4(&view.worldToVox, XMMatrixInverse(nullptr, voxToWorld));
    view.voxelWorldSize = XMVectorGetX(XMVector3Length(voxToWorld.r[0]));

    const Neuron::VoxelSurface& surface = view.model->surface;
    const XMFLOAT3 centerVox{static_cast<float>(surface.boundsMin.x + surface.boundsMax.x + 1) * 0.5f,
                             static_cast<float>(surface.boundsMin.y + surface.boundsMax.y + 1) * 0.5f,
                             static_cast<float>(surface.boundsMin.z + surface.boundsMax.z + 1) * 0.5f};
    view.bounds.center = TransformPoint(centerVox, voxToWorld);
    view.bounds.radius = XMFLOAT3{static_cast<float>(surface.boundsMax.x - surface.boundsMin.x + 1) * 0.5f * view.voxelWorldSize,
                                  static_cast<float>(surface.boundsMax.y - surface.boundsMin.y + 1) * 0.5f * view.voxelWorldSize,
                                  static_cast<float>(surface.boundsMax.z - surface.boundsMin.z + 1) * 0.5f * view.voxelWorldSize};
    XMFLOAT3X3 axes{};
    XMStoreFloat3x3(&axes, XMMATRIX(XMVector3Normalize(voxToWorld.r[0]), XMVector3Normalize(voxToWorld.r[1]),
                                    XMVector3Normalize(voxToWorld.r[2]), XMVectorZero()));
    view.bounds.axes = axes;
  }
}

void TacticalScene::BuildBlueprintLines()
{
  m_blueprintLines.clear();
  for (const InstanceView& view : m_instances)
  {
    if (view.kind != InstanceView::Kind::Blueprint)
    {
      continue;
    }
    const XMMATRIX voxToWorld = XMLoadFloat4x4(&view.voxToWorld);
    // A soft wide pass under a crisp one: additive blending turns the pair into a glowing edge.
    for (const XMFLOAT4& color : {BLUEPRINT_GLOW, BLUEPRINT_COLOR})
    {
      const float width = color.w < 0.5f ? 6.0f : 1.4f;
      for (const Neuron::LatticeSegment& edge : view.model->blueprintEdges)
      {
        m_blueprintLines.push_back(Neuron::GpuLineSegment{TransformPoint(ToFloat3(edge.a), voxToWorld), width,
                                                          TransformPoint(ToFloat3(edge.b), voxToWorld), 0.0f, color});
      }
    }
  }
  m_blueprintOrigin = m_origin;
  m_blueprintLinesValid = true;
}

void TacticalScene::BuildEffects(const UniverseSnapshot& _universe)
{
  const SceneDefinition& scene = m_assets->Scene();
  m_staticGlows.clear();
  m_plumes.clear();
  m_pointLights.clear();

  for (const InstanceView& view : m_instances)
  {
    if (view.kind == InstanceView::Kind::Blueprint || view.hidden)
    {
      continue;
    }
    const XMMATRIX voxToWorld = XMLoadFloat4x4(&view.voxToWorld);
    for (const GlowPoint& glow : view.model->glows)
    {
      const float strength = std::max({glow.color.x, glow.color.y, glow.color.z, 1e-3f});
      const XMFLOAT4 halo{glow.color.x / strength * 0.55f, glow.color.y / strength * 0.55f, glow.color.z / strength * 0.55f, 1.0f};
      const XMFLOAT4 core{1.0f, 0.95f, 0.85f, 0.55f};
      const XMFLOAT3 center = TransformPoint(glow.centerVox, voxToWorld);
      m_staticGlows.push_back(Neuron::GpuEffect{center,
                                                glow.radiusVoxels * view.voxelWorldSize,
                                                center,
                                                0.0f,
                                                halo,
                                                core,
                                                Neuron::EFFECT_GLOW,
                                                {glow.strobePeriodSeconds, 0.0f, 0.0f}});
    }
  }

  for (const EngineMount& engine : scene.engines)
  {
    const InstanceView* view = FindInstance(engine.instance);
    if (view == nullptr)
    {
      continue;
    }
    // A ship that flies shows its engine as the server last told it; anything else burns steadily.
    float strength = 1.0f;
    if (const ShipMotionState* motion = _universe.FindMotion(view->object))
    {
      const float throttle = std::clamp(std::abs(motion->throttle), 0.0f, 1.0f);
      strength = !motion->engineOn ? 0.0f : (motion->thruster ? 1.6f : PLUME_IDLE + (1.0f - PLUME_IDLE) * throttle);
      strength *= motion->cruise == CruiseState::Cruising ? 1.3f : 1.0f;
    }
    if (strength <= 0.0f)
    {
      continue;
    }
    const XMMATRIX voxToWorld = XMLoadFloat4x4(&view->voxToWorld);
    Plume plume{};
    plume.nozzle = TransformPoint(engine.positionVox, voxToWorld);
    XMStoreFloat3(&plume.direction, XMVector3Normalize(XMVectorNegate(voxToWorld.r[1]))); // ships face VOX +Y
    plume.radius = engine.radius;
    plume.length = engine.length;
    plume.phase = static_cast<float>(m_plumes.size()) * 1.7f;
    plume.strength = strength;
    m_plumes.push_back(plume);
  }

  for (const PointLightDef& light : scene.lights)
  {
    XMFLOAT3 position = ToRender(UniversePosition{static_cast<double>(light.position.x), static_cast<double>(light.position.y),
                                                  static_cast<double>(light.position.z)});
    if (!light.instance.empty())
    {
      const InstanceView* view = FindInstance(light.instance);
      if (view == nullptr)
      {
        continue;
      }
      position = TransformPoint(light.position, XMLoadFloat4x4(&view->voxToWorld));
    }
    m_pointLights.push_back(Neuron::GpuPointLight{position, light.radius, light.color, light.intensity});
  }
  for (const Plume& plume : m_plumes)
  {
    if (m_pointLights.size() >= Neuron::MAX_POINT_LIGHTS)
    {
      break;
    }
    const XMFLOAT3 glowPosition{plume.nozzle.x + plume.direction.x * plume.radius * 1.5f,
                                plume.nozzle.y + plume.direction.y * plume.radius * 1.5f,
                                plume.nozzle.z + plume.direction.z * plume.radius * 1.5f};
    m_pointLights.push_back(
      Neuron::GpuPointLight{glowPosition, plume.radius * 6.0f, XMFLOAT3{0.35f, 0.62f, 1.0f}, 1.6f * std::min(1.0f, plume.strength)});
  }
  m_pointLights.resize(std::min<std::size_t>(m_pointLights.size(), Neuron::MAX_POINT_LIGHTS));
}

void TacticalScene::BuildStars()
{
  const SceneDefinition& scene = m_assets->Scene();
  m_starDirections.clear();
  Neuron::Pcg32 rng(scene.stars.seed);
  for (int i = 0; i < scene.stars.count; ++i)
  {
    const float z = rng.Range(-1.0f, 1.0f);
    const float angle = rng.Range(0.0f, 2.0f * std::numbers::pi_v<float>);
    const float ring = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const float brightness = 0.25f + 0.75f * std::pow(rng.NextFloat(), 3.0f);
    m_starDirections.push_back(XMFLOAT4{ring * std::cos(angle), z, ring * std::sin(angle), brightness});
  }
}

void TacticalScene::BuildShadowCamera(const XMFLOAT3& _camera)
{
  // Fitted to what lies within SHADOW_RANGE_METRES of the camera: one cascade, which more can
  // later split by distance (ADR-010).
  XMVECTOR lo = XMVectorReplicate(std::numeric_limits<float>::max());
  XMVECTOR hi = XMVectorReplicate(-std::numeric_limits<float>::max());
  bool bounded = false;
  const XMVECTOR camera = XMLoadFloat3(&_camera);
  for (const InstanceView& view : m_instances)
  {
    if (view.kind == InstanceView::Kind::Blueprint || view.hidden ||
        XMVectorGetX(XMVector3Length(XMVectorSubtract(XMLoadFloat3(&view.bounds.center), camera))) > SHADOW_RANGE_METRES)
    {
      continue;
    }
    bounded = true;
    for (const XMFLOAT3& corner : Neuron::BoxCorners(view.bounds))
    {
      lo = XMVectorMin(lo, XMLoadFloat3(&corner));
      hi = XMVectorMax(hi, XMLoadFloat3(&corner));
    }
  }
  if (!bounded)
  {
    // Nothing to shadow: the client draws before the server has said what exists, or far from it all.
    lo = camera;
    hi = camera;
  }
  const XMVECTOR center = (lo + hi) * 0.5f;
  XMStoreFloat3(&m_sceneCenter, center);
  m_sceneRadius = std::max(1.0f, XMVectorGetX(XMVector3Length(hi - lo)) * 0.5f);

  const XMVECTOR toLight = XMVector3Normalize(XMLoadFloat3(&m_assets->Scene().keyLight.directionToLight));
  const XMVECTOR up = std::abs(XMVectorGetY(toLight)) > 0.95f ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
  const XMMATRIX view = XMMatrixLookAtLH(center + toLight * (m_sceneRadius * 2.0f), center, up);
  const XMMATRIX projection =
    XMMatrixOrthographicLH(m_sceneRadius * 2.0f, m_sceneRadius * 2.0f, m_sceneRadius * 0.5f, m_sceneRadius * 3.5f);
  XMStoreFloat4x4(&m_shadowViewProjection, view * projection);
}

const InstanceView* TacticalScene::FindInstance(std::string_view _name) const noexcept
{
  for (const InstanceView& view : m_instances)
  {
    if (view.name == _name)
    {
      return &view;
    }
  }
  return nullptr;
}

const InstanceView* TacticalScene::PickShip(const Neuron::Ray& _ray) const noexcept
{
  const InstanceView* best = nullptr;
  float bestDistance = std::numeric_limits<float>::max();
  for (const InstanceView& view : m_instances)
  {
    if (view.kind != InstanceView::Kind::Ship || view.hidden)
    {
      continue;
    }
    float distance = 0.0f;
    XMFLOAT3 normal{};
    if (Neuron::IntersectRayBox(view.bounds, _ray, true, distance, normal) && distance >= 0.0f && distance < bestDistance)
    {
      bestDistance = distance;
      best = &view;
    }
  }
  return best;
}

const InstanceView* TacticalScene::PickTarget(const Neuron::Ray& _ray) const noexcept
{
  const InstanceView* best = nullptr;
  float bestDistance = std::numeric_limits<float>::max();
  for (const InstanceView& view : m_instances)
  {
    if ((view.kind != InstanceView::Kind::Ship && view.kind != InstanceView::Kind::Station) || view.hidden)
    {
      continue;
    }
    float distance = 0.0f;
    XMFLOAT3 normal{};
    if (Neuron::IntersectRayBox(view.bounds, _ray, true, distance, normal) && distance >= 0.0f && distance < bestDistance)
    {
      bestDistance = distance;
      best = &view;
    }
  }
  return best;
}

Neuron::FrameConstants TacticalScene::MakeFrame(const ViewSetup& _view, float _timeSeconds, bool _withPointLights) const
{
  const KeyLightDefinition& key = m_assets->Scene().keyLight;
  Neuron::FrameConstants frame{};
  const XMMATRIX viewProjection = XMLoadFloat4x4(&_view.view) * XMLoadFloat4x4(&_view.projection);
  XMStoreFloat4x4(&frame.viewProjection, viewProjection);
  XMStoreFloat4x4(&frame.inverseViewProjection, XMMatrixInverse(nullptr, viewProjection));
  frame.shadowViewProjection = m_shadowViewProjection;
  frame.cameraPosition = _view.position;
  frame.time = _timeSeconds;
  frame.viewportOrigin = _view.viewportOrigin;
  frame.viewportSize = _view.viewportSize;
  XMStoreFloat3(&frame.keyLightDirection, XMVector3Normalize(XMLoadFloat3(&key.directionToLight)));
  frame.shadowEnabled = _withPointLights ? 1.0f : 0.0f;
  frame.keyLightColor = XMFLOAT3{key.color.x * key.intensity, key.color.y * key.intensity, key.color.z * key.intensity};
  frame.ambientSky = key.ambientSky;
  frame.ambientGround = key.ambientGround;
  frame.exposure = EXPOSURE;
  frame.shadowTexelSize = 1.0f / static_cast<float>(Neuron::Renderer::SHADOW_MAP_SIZE);
  frame.cameraForward = _view.forward;
  frame.isOrthographic = _view.orthographic ? 1.0f : 0.0f;
  frame.nearZ = _view.nearZ;
  frame.renderTargetHeight = _view.viewportSize.y;
  if (_withPointLights)
  {
    frame.pointLightCount = static_cast<float>(m_pointLights.size());
    std::copy(m_pointLights.begin(), m_pointLights.end(), frame.pointLights);
  }
  return frame;
}

Neuron::VoxelDrawConstants TacticalScene::DrawConstants(FXMMATRIX _voxToWorld, float _voxelWorldSize, const ViewSetup& _view)
{
  Neuron::VoxelDrawConstants constants{};
  const XMMATRIX voxToClip = _voxToWorld * XMLoadFloat4x4(&_view.view) * XMLoadFloat4x4(&_view.projection);
  const XMMATRIX worldToVox = XMMatrixInverse(nullptr, _voxToWorld);
  XMStoreFloat4x4(&constants.voxToWorld, _voxToWorld);
  XMStoreFloat4x4(&constants.voxToClip, voxToClip);
  XMStoreFloat4x4(&constants.clipToVox, XMMatrixInverse(nullptr, voxToClip));
  XMStoreFloat3(&constants.cameraForwardVox, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&_view.forward), worldToVox)));
  XMStoreFloat3(&constants.cameraPositionVox, XMVector3TransformCoord(XMLoadFloat3(&_view.position), worldToVox));
  constants.voxelWorldSize = _voxelWorldSize;
  constants.emissionScale = 1.0f;
  constants.tint = XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f};
  constants.rimColor = XMFLOAT3{0.0f, 0.0f, 0.0f};
  constants.rimStrength = 0.0f;
  return constants;
}

void TacticalScene::RenderShadows(Neuron::Renderer& _renderer) const
{
  ViewSetup light;
  light.orthographic = true;
  light.nearZ = 0.0f;
  light.viewportSize =
    XMFLOAT2{static_cast<float>(Neuron::Renderer::SHADOW_MAP_SIZE), static_cast<float>(Neuron::Renderer::SHADOW_MAP_SIZE)};
  XMStoreFloat4x4(&light.view, XMMatrixIdentity());
  light.projection = m_shadowViewProjection;
  const XMVECTOR toLight = XMVector3Normalize(XMLoadFloat3(&m_assets->Scene().keyLight.directionToLight));
  XMStoreFloat3(&light.forward, XMVectorNegate(toLight));
  XMStoreFloat3(&light.position, XMLoadFloat3(&m_sceneCenter) + toLight * (m_sceneRadius * 2.0f));

  Neuron::FrameConstants frame = MakeFrame(light, 0.0f, false);
  _renderer.BeginShadowPass(frame);
  for (const InstanceView& view : m_instances)
  {
    if (view.kind == InstanceView::Kind::Blueprint || view.kind == InstanceView::Kind::Debris || view.hidden ||
        view.model->gpu.voxelCount == 0)
    {
      continue;
    }
    _renderer.DrawVoxelsShadow(view.model->gpu, DrawConstants(XMLoadFloat4x4(&view.voxToWorld), view.voxelWorldSize, light));
  }
  _renderer.EndShadowPass();
}

void TacticalScene::Render(Neuron::Renderer& _renderer, const ViewSetup& _view, const InstanceView* _selected, float _timeSeconds) const
{
  for (const InstanceView& view : m_instances)
  {
    if (view.kind == InstanceView::Kind::Blueprint || view.hidden || view.model->gpu.voxelCount == 0)
    {
      continue;
    }
    Neuron::VoxelDrawConstants constants = DrawConstants(XMLoadFloat4x4(&view.voxToWorld), view.voxelWorldSize, _view);
    if (&view == _selected)
    {
      constants.rimColor = SELECTION_RIM;
      constants.rimStrength = 0.30f + 0.08f * std::sin(_timeSeconds * 3.0f);
    }
    _renderer.DrawVoxels(view.model->gpu, constants);
  }

  _renderer.DrawLines(m_blueprintLines);

  std::vector<Neuron::GpuEffect> effects;
  effects.reserve(m_starDirections.size() + m_staticGlows.size() + m_plumes.size() * 2 + m_combatEffects.size());
  for (const XMFLOAT4& star : m_starDirections)
  {
    const XMFLOAT3 position{_view.position.x + star.x * STAR_DISTANCE, _view.position.y + star.y * STAR_DISTANCE,
                            _view.position.z + star.z * STAR_DISTANCE};
    const float size = 1.2f + star.w * 1.8f;
    const XMFLOAT4 halo{0.55f * star.w, 0.65f * star.w, 0.85f * star.w, 0.6f};
    const XMFLOAT4 core{0.9f, 0.95f, 1.0f, star.w};
    effects.push_back(Neuron::GpuEffect{
      position, size, position, size, halo, core, Neuron::EFFECT_GLOW | Neuron::EFFECT_WIDTH_IN_PIXELS, {0.0f, 0.0f, 0.0f}});
  }
  effects.insert(effects.end(), m_staticGlows.begin(), m_staticGlows.end());
  effects.insert(effects.end(), m_combatEffects.begin(), m_combatEffects.end());
  for (const Plume& plume : m_plumes)
  {
    const float flicker = 0.94f + 0.06f * std::sin(_timeSeconds * 23.0f + plume.phase) * std::sin(_timeSeconds * 7.0f + plume.phase * 2.0f);
    const float length = plume.length * flicker * plume.strength;
    const XMFLOAT3 tail{plume.nozzle.x + plume.direction.x * length, plume.nozzle.y + plume.direction.y * length,
                        plume.nozzle.z + plume.direction.z * length};
    effects.push_back(Neuron::GpuEffect{plume.nozzle,
                                        plume.radius * 1.15f,
                                        tail,
                                        plume.radius * 0.25f,
                                        XMFLOAT4{0.30f, 0.62f, 1.0f, 0.95f},
                                        XMFLOAT4{0.10f, 0.25f, 1.0f, 0.0f},
                                        0u,
                                        {0.0f, 0.0f, 0.0f}});
    const XMFLOAT3 glow{plume.nozzle.x + plume.direction.x * 0.6f, plume.nozzle.y + plume.direction.y * 0.6f,
                        plume.nozzle.z + plume.direction.z * 0.6f};
    effects.push_back(Neuron::GpuEffect{glow,
                                        plume.radius * 2.8f,
                                        glow,
                                        0.0f,
                                        XMFLOAT4{0.22f, 0.50f, 1.0f, 0.65f},
                                        XMFLOAT4{0.85f, 0.95f, 1.0f, 0.95f},
                                        Neuron::EFFECT_GLOW,
                                        {0.0f, 0.0f, 0.0f}});
  }
  _renderer.DrawEffects(effects);
}

void TacticalScene::RenderModelAlone(Neuron::Renderer& _renderer, const LoadedModel& _model, const ModelAsset& _asset,
                                     const ViewSetup& _view, float _yawDegrees)
{
  const XMMATRIX voxToWorld =
    Neuron::VoxToModelMatrix(_asset.pivotVox, _asset.voxelSize) * XMMatrixRotationY(XMConvertToRadians(_yawDegrees));
  _renderer.DrawVoxels(_model.gpu, DrawConstants(voxToWorld, _asset.voxelSize, _view));
}

} // namespace Outpost
