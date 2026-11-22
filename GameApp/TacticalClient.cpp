#include "TacticalClient.h"

#include "Messages.h"
#include "ModelAxes.h"
#include "ScreenProjection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

namespace Outpost
{

using namespace DirectX;

namespace
{

constexpr float CLEAR_COLOR[4] = {0.0035f, 0.0055f, 0.0110f, 1.0f};
constexpr float THUMBNAIL_DEGREES_PER_SECOND = 14.0f;
constexpr float RADAR_RANGE_WORLD = 560.0f;

[[nodiscard]] Neuron::CameraPose PoseFromScene(const CameraDefinition& _camera) noexcept
{
  return Neuron::CameraPose{_camera.target, _camera.yawDegrees, _camera.pitchDegrees, _camera.distance, _camera.fovYDegrees};
}

[[nodiscard]] float HudScaleFor(std::uint32_t _height) noexcept
{
  const float scale = std::clamp(static_cast<float>(_height) / 1080.0f, 0.6f, 2.0f);
  return std::round(scale * 20.0f) / 20.0f;
}

} // namespace

TacticalClient::~TacticalClient()
{
  // The renderer, the model buffers and the HUD atlas are released after this body runs, and the
  // last frame that used them may still be executing. Releasing a resource the GPU is reading is
  // undefined; in practice it stalled the driver on shutdown after a resize.
  if (m_device != nullptr)
  {
    m_device->WaitIdle();
  }
}

bool TacticalClient::Initialize(Neuron::GraphicsDevice& _device, const std::filesystem::path& _assetRoot, std::string& _outError)
{
  m_device = &_device;
  if (!m_renderer.Initialize(_device, _outError))
  {
    return false;
  }
  if (!m_assets.Load(_assetRoot, m_renderer, _outError))
  {
    return false;
  }
  // Nothing is drawn until the server has said what exists (R25); the selection waits for it.
  m_scene.Build(m_assets, m_world);
  m_builtRevision = m_world.objectsRevision;
  ResetView();
  m_initialSelection = m_assets.Scene().selectedShip;
  if (!RebuildFonts(HudScaleFor(_device.Height())))
  {
    _outError = "HUD font atlas could not be built";
    return false;
  }
  return true;
}

bool TacticalClient::Resize(std::uint32_t /*_width*/, std::uint32_t _height)
{
  const float scale = HudScaleFor(_height);
  return scale == m_hudScale || RebuildFonts(scale);
}

bool TacticalClient::RebuildFonts(float _scale)
{
  if (!m_fonts.Build(TacticalHud::Fonts(_scale)))
  {
    return false;
  }
  m_hudScale = _scale;
  return m_renderer.SetHudAtlas(m_fonts.Width(), m_fonts.Height(), m_fonts.Coverage());
}

void TacticalClient::ResetView()
{
  m_camera.SetPose(PoseFromScene(m_assets.Scene().camera));
}

void TacticalClient::Select(std::string_view _instance)
{
  const InstanceView* view = _instance.empty() ? nullptr : m_scene.FindInstance(_instance);
  m_selected = (view != nullptr && view->kind == InstanceView::Kind::Ship) ? std::string(_instance) : std::string();
  BuildSilhouette();
}

void TacticalClient::OnPointerDown(PointerButton _button, float _x, float _y, std::uint64_t _timeMilliseconds)
{
  m_lastX = _x;
  m_lastY = _y;
  if (_button == PointerButton::Left)
  {
    m_gesture.Press(_x, _y, _timeMilliseconds);
  }
  else
  {
    m_panning = true;
  }
}

void TacticalClient::OnPointerMove(float _x, float _y)
{
  if (m_gesture.IsPressed())
  {
    float dx = 0.0f;
    float dy = 0.0f;
    if (m_gesture.Move(_x, _y, dx, dy))
    {
      m_camera.Orbit(dx, dy);
    }
  }
  else if (m_panning)
  {
    m_camera.Pan(_x - m_lastX, _y - m_lastY, static_cast<float>(m_device->Height()));
  }
  m_lastX = _x;
  m_lastY = _y;
}

void TacticalClient::OnPointerUp(PointerButton _button, float _x, float _y, std::uint64_t _timeMilliseconds)
{
  if (_button != PointerButton::Left)
  {
    m_panning = false;
    return;
  }
  if (!m_gesture.Release(_x, _y, _timeMilliseconds))
  {
    return; // that was a drag
  }
  const ViewSetup view = MainView();
  const XMMATRIX viewProjection = XMLoadFloat4x4(&view.view) * XMLoadFloat4x4(&view.projection);
  const Neuron::Ray ray = Neuron::PickRay(_x, _y, XMMatrixInverse(nullptr, viewProjection), view.viewportSize.x, view.viewportSize.y);
  const InstanceView* picked = m_scene.PickShip(ray);
  Select(picked != nullptr ? std::string_view(picked->name) : std::string_view());
}

void TacticalClient::OnPointerCancel()
{
  m_gesture.Cancel();
  m_panning = false;
}

void TacticalClient::OnWheel(float _steps)
{
  m_camera.Zoom(_steps);
}

ViewSetup TacticalClient::MainView() const
{
  ViewSetup view;
  const float width = static_cast<float>(m_device->Width());
  const float height = static_cast<float>(m_device->Height());
  XMStoreFloat4x4(&view.view, m_camera.View());
  XMStoreFloat4x4(&view.projection, m_camera.Projection(width / std::max(height, 1.0f)));
  view.position = m_camera.Position();
  view.forward = m_camera.Forward();
  view.viewportSize = XMFLOAT2{width, height};
  view.nearZ = Neuron::OrbitCamera::NEAR_Z;
  return view;
}

void TacticalClient::BuildSilhouette()
{
  if (m_silhouetteFor == m_selected)
  {
    return;
  }
  m_silhouetteFor = m_selected;
  m_silhouette.clear();
  const InstanceView* view = m_selected.empty() ? nullptr : m_scene.FindInstance(m_selected);
  if (view == nullptr)
  {
    return;
  }
  const XMMATRIX voxToModel = Neuron::VoxToModelMatrix(view->asset->pivotVox, view->asset->voxelSize);
  XMVECTOR lo = XMVectorReplicate(1e9f);
  XMVECTOR hi = XMVectorReplicate(-1e9f);
  struct Pass
  {
    const std::vector<Neuron::LatticeSegment>* edges;
    XMFLOAT4 color;
    float widthPixels;
  };
  const Neuron::SideProfile& profile = view->model->sideProfile;
  const Pass passes[] = {{&profile.outline, XMFLOAT4{0.10f, 0.55f, 1.0f, 0.12f}, 4.0f},
                         {&profile.detail, XMFLOAT4{0.15f, 0.65f, 1.0f, 0.30f}, 1.0f},
                         {&profile.outline, XMFLOAT4{0.30f, 0.90f, 1.0f, 0.90f}, 1.3f}};
  for (const Pass& pass : passes)
  {
    const XMFLOAT4& color = pass.color;
    const float width = pass.widthPixels;
    for (const Neuron::LatticeSegment& edge : *pass.edges)
    {
      Neuron::GpuLineSegment segment{};
      const XMVECTOR a = XMVector3TransformCoord(
        XMVectorSet(static_cast<float>(edge.a.x), static_cast<float>(edge.a.y), static_cast<float>(edge.a.z), 1.0f), voxToModel);
      const XMVECTOR b = XMVector3TransformCoord(
        XMVectorSet(static_cast<float>(edge.b.x), static_cast<float>(edge.b.y), static_cast<float>(edge.b.z), 1.0f), voxToModel);
      XMStoreFloat3(&segment.a, a);
      XMStoreFloat3(&segment.b, b);
      segment.widthPixels = width;
      segment.color = color;
      lo = XMVectorMin(lo, XMVectorMin(a, b));
      hi = XMVectorMax(hi, XMVectorMax(a, b));
      m_silhouette.push_back(segment);
    }
  }
  XMStoreFloat3(&m_silhouetteMin, lo);
  XMStoreFloat3(&m_silhouetteMax, hi);
}

void TacticalClient::Apply(const InputEvent& _event, std::uint64_t _timeMilliseconds)
{
  switch (_event.kind)
  {
  case InputEvent::Kind::PointerDown:
    OnPointerDown(_event.button, _event.xPixels, _event.yPixels, _timeMilliseconds);
    break;
  case InputEvent::Kind::PointerMove:
    OnPointerMove(_event.xPixels, _event.yPixels);
    break;
  case InputEvent::Kind::PointerUp:
    OnPointerUp(_event.button, _event.xPixels, _event.yPixels, _timeMilliseconds);
    break;
  case InputEvent::Kind::Wheel:
    OnWheel(_event.wheelSteps);
    break;
  case InputEvent::Kind::ResetView:
    ResetView();
    break;
  case InputEvent::Kind::ToggleHelp:
    ToggleHelp();
    break;
  case InputEvent::Kind::ToggleStatus:
    ToggleStatus();
    break;
  }
}

bool TacticalClient::Receive(Neuron::LoopbackTransport& _transport)
{
  while (std::optional<Neuron::Packet> packet = _transport.Receive(Neuron::Peer::Client))
  {
    const auto message = Decode(packet->bytes);
    if (!message.has_value())
    {
      m_protocolError = "a message from the server did not decode (error " + std::to_string(static_cast<int>(message.error())) + ")";
      return false;
    }
    ApplyMessage(*message, m_world);
  }
  return true;
}

void TacticalClient::RebuildSceneIfChanged()
{
  if (m_world.objectsRevision == m_builtRevision)
  {
    return;
  }
  m_scene.Build(m_assets, m_world);
  m_builtRevision = m_world.objectsRevision;
  m_shadowDirty = true;
  if (!m_initialSelection.empty() && m_scene.FindInstance(m_initialSelection) != nullptr)
  {
    Select(std::exchange(m_initialSelection, std::string()));
  }
}

HudInput TacticalClient::MakeHudInput(const ViewSetup& _view, float _timeSeconds, float _framesPerSecond) const
{
  const WorldSnapshot& snapshot = m_world;
  HudInput input;
  input.width = _view.viewportSize.x;
  input.height = _view.viewportSize.y;
  input.scale = m_hudScale;
  input.timeSeconds = _timeSeconds;
  input.snapshot = &snapshot;
  input.selected = m_selected.empty() ? nullptr : snapshot.FindShip(m_selected);
  input.radarRangeWorld = RADAR_RANGE_WORLD;
  input.cameraYawDegrees = m_camera.Pose().yawDegrees;
  input.cameraFovDegrees = m_camera.Pose().fovYDegrees;
  input.showHelp = m_showHelp;

  const XMMATRIX viewProjection = XMLoadFloat4x4(&_view.view) * XMLoadFloat4x4(&_view.projection);
  const XMFLOAT3 radarCenter = m_camera.Pose().target;
  for (const InstanceView& view : m_scene.Instances())
  {
    RadarBlip blip;
    blip.x = view.bounds.center.x - radarCenter.x;
    blip.z = view.bounds.center.z - radarCenter.z;
    blip.sizeWorld = std::max(view.bounds.radius.x, std::max(view.bounds.radius.y, view.bounds.radius.z)) * 2.0f;
    switch (view.kind)
    {
    case InstanceView::Kind::Ship:
    {
      const ShipState* ship = snapshot.FindShip(view.name);
      const bool selected = view.name == m_selected;
      blip.kind = selected ? RadarBlip::Kind::SelectedShip : RadarBlip::Kind::Ship;
      ShipMarker marker;
      marker.callsign = ship != nullptr ? ship->callsign : view.name;
      marker.hullPercent = ship != nullptr ? ship->hullPercent : 100;
      marker.selected = selected;
      marker.bounds = Neuron::ProjectBox(Neuron::BoxCorners(view.bounds), viewProjection, input.width, input.height);
      input.markers.push_back(std::move(marker));
      break;
    }
    case InstanceView::Kind::Rock:
      blip.kind = RadarBlip::Kind::Rock;
      break;
    case InstanceView::Kind::Blueprint:
      blip.kind = RadarBlip::Kind::Blueprint;
      break;
    case InstanceView::Kind::Debris:
      continue;
    default:
      blip.kind = RadarBlip::Kind::Station;
      break;
    }
    input.blips.push_back(blip);
  }
  for (const BuildItemDefinition& item : m_assets.Scene().buildItems)
  {
    input.buildLabels.push_back(item.label);
  }
  if (m_showStatus)
  {
    char status[256]{};
    std::snprintf(status, sizeof(status), "%ls  |  %zu models, %zu surface voxels  |  %.0f fps", m_device->AdapterName().c_str(),
                  m_assets.ModelCount(), m_assets.SurfaceVoxelCount(), static_cast<double>(_framesPerSecond));
    input.statusLine = status;
  }
  return input;
}

void TacticalClient::RenderThumbnails(const HudLayout& _layout, float _timeSeconds)
{
  const auto& items = m_assets.Scene().buildItems;
  for (std::size_t i = 0; i < _layout.buildSlots.size() && i < items.size(); ++i)
  {
    const ModelAsset* asset = m_assets.Scene().FindModel(items[i].model);
    const LoadedModel* model = m_assets.FindModel(items[i].model);
    if (asset == nullptr || model == nullptr)
    {
      continue;
    }
    const Neuron::ScreenRect& rect = _layout.buildSlots[i];
    const float width = std::floor(rect.right - rect.left);
    const float height = std::floor(rect.bottom - rect.top);
    const XMFLOAT3 size{static_cast<float>(asset->size.x), static_cast<float>(asset->size.z), static_cast<float>(asset->size.y)};
    const float radius = 0.5f * asset->voxelSize * std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
    constexpr float FOV_DEGREES = 30.0f;
    const float distance = radius / std::sin(XMConvertToRadians(FOV_DEGREES * 0.5f)) * 0.92f;
    const float pitch = XMConvertToRadians(26.0f);
    const XMVECTOR eye = XMVectorSet(0.0f, std::sin(pitch) * distance, -std::cos(pitch) * distance, 1.0f);

    ViewSetup view;
    view.viewportOrigin = XMFLOAT2{std::floor(rect.left), std::floor(rect.top)};
    view.viewportSize = XMFLOAT2{width, height};
    view.nearZ = std::max(0.5f, distance - radius * 1.5f);
    XMStoreFloat4x4(&view.view, XMMatrixLookAtLH(eye, XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
    XMStoreFloat4x4(&view.projection, XMMatrixPerspectiveFovLH(XMConvertToRadians(FOV_DEGREES), width / std::max(height, 1.0f), view.nearZ,
                                                               distance + radius * 2.0f));
    XMStoreFloat3(&view.position, eye);
    XMStoreFloat3(&view.forward, XMVector3Normalize(XMVectorNegate(eye)));

    m_renderer.BeginPass(m_scene.MakeFrame(view, _timeSeconds, false));
    m_renderer.ClearDepthInViewport();
    TacticalScene::RenderModelAlone(m_renderer, *model, *asset, view, 215.0f + _timeSeconds * THUMBNAIL_DEGREES_PER_SECOND);
  }
}

void TacticalClient::RenderSilhouette(const HudLayout& _layout, float _timeSeconds)
{
  const Neuron::ScreenRect& rect = _layout.silhouette;
  if (m_silhouette.empty() || rect.right - rect.left < 8.0f)
  {
    return;
  }
  const float width = std::floor(rect.right - rect.left);
  const float height = std::floor(rect.bottom - rect.top);
  const float aspect = width / std::max(height, 1.0f);
  // Side elevation from +X: model +Z (the bow) points right, +Y is up.
  const float length = m_silhouetteMax.z - m_silhouetteMin.z;
  const float tall = m_silhouetteMax.y - m_silhouetteMin.y;
  const float viewWidth = std::max(length * 1.08f, tall * 1.08f * aspect);
  const XMFLOAT3 center{(m_silhouetteMin.x + m_silhouetteMax.x) * 0.5f, (m_silhouetteMin.y + m_silhouetteMax.y) * 0.5f,
                        (m_silhouetteMin.z + m_silhouetteMax.z) * 0.5f};
  const float depth = (m_silhouetteMax.x - m_silhouetteMin.x) + 20.0f;
  const XMVECTOR target = XMLoadFloat3(&center);
  const XMVECTOR eye = target + XMVectorSet(depth, 0.0f, 0.0f, 0.0f);

  ViewSetup view;
  view.orthographic = true;
  view.nearZ = 0.0f;
  view.viewportOrigin = XMFLOAT2{std::floor(rect.left), std::floor(rect.top)};
  view.viewportSize = XMFLOAT2{width, height};
  XMStoreFloat4x4(&view.view, XMMatrixLookAtLH(eye, target, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
  XMStoreFloat4x4(&view.projection, XMMatrixOrthographicLH(viewWidth, viewWidth / aspect, 1.0f, depth * 2.0f));
  XMStoreFloat3(&view.position, eye);
  view.forward = XMFLOAT3{-1.0f, 0.0f, 0.0f};
  m_renderer.BeginPass(m_scene.MakeFrame(view, _timeSeconds, false));
  m_renderer.ClearDepthInViewport();
  m_renderer.DrawLines(m_silhouette);
}

bool TacticalClient::Render(float _timeSeconds, bool _capture, bool _vsync, float _framesPerSecond)
{
  RebuildSceneIfChanged();
  if (m_device->BeginFrame() == nullptr)
  {
    return false;
  }
  m_renderer.ClearMainTargets(CLEAR_COLOR);
  if (m_shadowDirty || m_shadowsEveryFrame)
  {
    // The scene is static, so the key light's shadow map is drawn once and kept.
    m_scene.RenderShadows(m_renderer);
    m_shadowDirty = false;
  }
  m_device->Timestamp();

  const ViewSetup view = MainView();
  m_renderer.BeginPass(m_scene.MakeFrame(view, _timeSeconds, true));
  const InstanceView* selected = m_selected.empty() ? nullptr : m_scene.FindInstance(m_selected);
  m_scene.Render(m_renderer, view, selected, _timeSeconds);
  m_device->Timestamp();

  m_batch.Begin(&m_fonts);
  const HudInput input = MakeHudInput(view, _timeSeconds, _framesPerSecond);
  const HudLayout layout = m_hud.Build(m_batch, input);
  Neuron::FrameConstants hudFrame{};
  hudFrame.viewportSize = view.viewportSize;
  hudFrame.exposure = 1.0f;
  m_renderer.BeginPass(hudFrame);
  m_renderer.DrawHud(m_batch.Vertices(Neuron::HudBatch::BACK));
  RenderThumbnails(layout, _timeSeconds);
  RenderSilhouette(layout, _timeSeconds);
  m_renderer.BeginPass(hudFrame);
  m_renderer.DrawHud(m_batch.Vertices(Neuron::HudBatch::FRONT));
  return m_device->EndFrame(_capture, _vsync);
}

} // namespace Outpost
