#include "TacticalClient.h"

#include "Collision.h"
#include "Launcher.h"
#include "MaskEvent.h"
#include "Messages.h"
#include "ModelAxes.h"
#include "ScreenProjection.h"
#include "VoxelCarve.h"

#include <algorithm>
#include <array>
#include <cctype>
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
// A frame longer than this is a stall (a breakpoint, a drag of the window): the camera does not
// try to catch up with it.
constexpr float MAX_FRAME_SECONDS = 0.1f;

[[nodiscard]] Neuron::CameraPose PoseFromScene(const CameraDefinition& _camera) noexcept
{
  return Neuron::CameraPose{_camera.target, _camera.yawDegrees, _camera.pitchDegrees, _camera.distance, _camera.fovYDegrees};
}

[[nodiscard]] float HudScaleFor(std::uint32_t _height) noexcept
{
  const float scale = std::clamp(static_cast<float>(_height) / 1080.0f, 0.6f, 2.0f);
  return std::round(scale * 20.0f) / 20.0f;
}

[[nodiscard]] Neuron::InputKey MouseKey(PointerButton _button) noexcept
{
  switch (_button)
  {
  case PointerButton::Right:
    return Neuron::KEY_MOUSE_RIGHT;
  case PointerButton::Middle:
    return Neuron::KEY_MOUSE_MIDDLE;
  default:
    return Neuron::KEY_MOUSE_LEFT;
  }
}

[[nodiscard]] UniversePosition Offset(const UniversePosition& _position, const XMFLOAT3& _offset) noexcept
{
  return UniversePosition{_position.x + static_cast<double>(_offset.x), _position.y + static_cast<double>(_offset.y),
                          _position.z + static_cast<double>(_offset.z)};
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
  // The UI atlases and their manifest (ADR-023), read from the asset root like all content.
  std::vector<Neuron::DdsImage> atlases;
  if (!Neuron::LoadSpriteSheet(_assetRoot / "Ui" / "Sprites.txt", m_sprites, atlases, _outError))
  {
    return false;
  }
  for (std::size_t i = 0; i < atlases.size(); ++i)
  {
    if (!m_renderer.SetHudTexture(m_sprites.Atlases()[i].textureSlot, atlases[i], _outError))
    {
      _outError.insert(0, ": ").insert(0, m_sprites.Atlases()[i].file);
      return false;
    }
  }
  // The client reads the content it needs to fly its own ship itself (ADR-009): the archetypes for
  // its flight model and chase camera, and the bindings for its commands.
  if (!LoadShipContent(_assetRoot / "Content" / "Ships.txt", m_ships, _outError))
  {
    return false;
  }
  std::vector<std::string> problems = ResolveShipModels(m_ships, m_assets.Scene());
  if (!problems.empty())
  {
    _outError = problems.front();
    return false;
  }
  // The hulls, so that the own ship's muzzles and condition are the server's (plan tasks 3.4, 3.6),
  // and every archetype's voxels, so that the masks the server sends apply here as there (R22).
  auto combat = std::make_shared<CombatContent>();
  if (!LoadCombatContent(_assetRoot, m_assets.Scene(), *combat, _outError))
  {
    return false;
  }
  m_hulls = combat->hulls;
  std::vector<const Neuron::VoxModel*> sceneModels;
  for (const VoxelArchetype& archetype : combat->archetypes)
  {
    const LoadedModel* model = m_assets.FindModel(archetype.model);
    sceneModels.push_back(model != nullptr ? &model->voxels : nullptr);
  }
  m_combat.Load(combat, m_assets.Scene());
  m_surfaces.Load(combat, std::move(sceneModels));
  problems = ResolveMounts(m_ships, m_assets.Scene(), m_hulls);
  if (!problems.empty())
  {
    _outError = problems.front();
    return false;
  }
  if (!LoadSystemContent(_assetRoot / "Content" / "System.txt", m_assets.Scene(), m_system, _outError))
  {
    return false;
  }
  if (!Neuron::CommandMap::Load(_assetRoot / "Content" / "Bindings.txt", m_commands, _outError))
  {
    return false;
  }
  m_flightInput.Bind(m_commands);
  m_commands.SetContextActive(m_commands.FindContext("Always"), true);
  SetView(ClientView::Flight);

  // Nothing is drawn until the server has said what exists (R25); the selection waits for it.
  m_scene.Build(m_assets, m_universe);
  m_builtRevision = m_universe.objectsRevision;
  ResetView();
  m_initialSelection = m_assets.Scene().selectedShip;
  m_flightInput.SetCursor(static_cast<float>(_device.Width()) * 0.5f, static_cast<float>(_device.Height()) * 0.5f,
                          static_cast<float>(_device.Width()), static_cast<float>(_device.Height()));
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

void TacticalClient::SetView(ClientView _view)
{
  m_view = _view;
  m_commands.SetContextActive(m_commands.FindContext("Flying"), _view == ClientView::Flight);
  m_commands.SetContextActive(m_commands.FindContext("Tactical"), _view == ClientView::Tactical);
  m_flightInput.SetMouseEnabled(_view == ClientView::Flight);
  m_gesture.Cancel();
  m_orderGesture.Cancel();
  m_panning = false;
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
  m_flightInput.SetCursor(_x, _y, static_cast<float>(m_device->Width()), static_cast<float>(m_device->Height()));
  if (m_view == ClientView::Flight)
  {
    m_commands.KeyDown(MouseKey(_button), false);
    return;
  }
  if (_button == PointerButton::Left)
  {
    m_controls.PointerMove(_x, _y);
    m_uiPress = m_controls.PointerDown();
    if (!m_uiPress)
    {
      m_gesture.Press(_x, _y, _timeMilliseconds);
    }
  }
  else if (_button == PointerButton::Right)
  {
    m_orderGesture.Press(_x, _y, _timeMilliseconds);
  }
  else
  {
    m_panning = true;
  }
}

void TacticalClient::OnPointerMove(float _x, float _y)
{
  m_flightInput.SetCursor(_x, _y, static_cast<float>(m_device->Width()), static_cast<float>(m_device->Height()));
  if (m_view == ClientView::Tactical)
  {
    m_controls.PointerMove(_x, _y);
    float dx = 0.0f;
    float dy = 0.0f;
    if (m_gesture.IsPressed())
    {
      if (m_gesture.Move(_x, _y, dx, dy))
      {
        m_camera.Orbit(dx, dy);
      }
    }
    else if (m_orderGesture.IsPressed())
    {
      // A right drag pans, as the middle button does; a right click orders.
      if (m_orderGesture.Move(_x, _y, dx, dy))
      {
        m_camera.Pan(dx, dy, static_cast<float>(m_device->Height()));
      }
    }
    else if (m_panning)
    {
      m_camera.Pan(_x - m_lastX, _y - m_lastY, static_cast<float>(m_device->Height()));
    }
  }
  m_lastX = _x;
  m_lastY = _y;
}

void TacticalClient::OnPointerUp(PointerButton _button, float _x, float _y, std::uint64_t _timeMilliseconds)
{
  // A button released after the view changed still releases what its press held.
  m_commands.KeyUp(MouseKey(_button));
  if (m_view == ClientView::Flight)
  {
    return;
  }
  if (_button == PointerButton::Right)
  {
    if (m_orderGesture.IsPressed() && m_orderGesture.Release(_x, _y, _timeMilliseconds))
    {
      OrderAt(_x, _y);
    }
    return;
  }
  if (_button != PointerButton::Left)
  {
    m_panning = false;
    return;
  }
  if (m_uiPress)
  {
    m_uiPress = false;
    m_controls.PointerUp();
    return;
  }
  if (!m_gesture.IsPressed() || !m_gesture.Release(_x, _y, _timeMilliseconds))
  {
    return; // that was a drag
  }
  const XMMATRIX viewProjection = XMLoadFloat4x4(&m_lastView.view) * XMLoadFloat4x4(&m_lastView.projection);
  const Neuron::Ray ray =
    Neuron::PickRay(_x, _y, XMMatrixInverse(nullptr, viewProjection), m_lastView.viewportSize.x, m_lastView.viewportSize.y);
  const InstanceView* picked = m_scene.PickShip(ray);
  Select(picked != nullptr ? std::string_view(picked->name) : std::string_view());
}

void TacticalClient::OnPointerCancel()
{
  m_controls.PointerLeave();
  m_uiPress = false;
  m_gesture.Cancel();
  m_orderGesture.Cancel();
  m_panning = false;
  m_commands.KeyUp(Neuron::KEY_MOUSE_LEFT);
  m_commands.KeyUp(Neuron::KEY_MOUSE_RIGHT);
  m_commands.KeyUp(Neuron::KEY_MOUSE_MIDDLE);
}

void TacticalClient::OnWheel(float _steps)
{
  if (m_view == ClientView::Tactical)
  {
    m_camera.Zoom(_steps);
    return;
  }
  const int notches = std::max(1, static_cast<int>(std::lround(std::abs(_steps))));
  for (int i = 0; i < notches && _steps != 0.0f; ++i)
  {
    m_commands.Wheel(_steps > 0.0f ? Neuron::KEY_WHEEL_UP : Neuron::KEY_WHEEL_DOWN);
  }
}

void TacticalClient::OnKey(Neuron::InputKey _key, bool _down, bool _repeat)
{
  if (_down)
  {
    m_commands.KeyDown(_key, _repeat);
  }
  else
  {
    m_commands.KeyUp(_key);
  }
}

void TacticalClient::OnFocusLost()
{
  m_commands.ReleaseAll();
  OnPointerCancel();
}

void TacticalClient::OrderAt(float _x, float _y)
{
  if (!m_prediction.Active())
  {
    return;
  }
  const XMMATRIX viewProjection = XMLoadFloat4x4(&m_lastView.view) * XMLoadFloat4x4(&m_lastView.projection);
  const Neuron::Ray ray =
    Neuron::PickRay(_x, _y, XMMatrixInverse(nullptr, viewProjection), m_lastView.viewportSize.x, m_lastView.viewportSize.y);
  // On a ship: to it, stopping short. Anywhere else: to the point on the own ship's level.
  const InstanceView* picked = m_scene.PickShip(ray);
  if (picked != nullptr && picked->object != m_universe.ownShip)
  {
    const float radius = std::max({picked->bounds.radius.x, picked->bounds.radius.y, picked->bounds.radius.z});
    m_flightInput.GoTo(picked->pose.position, radius * ORDER_STAND_OFF_RADII);
    return;
  }
  const XMFLOAT3 ship = m_scene.ToRender(m_prediction.State().position);
  if (std::abs(ray.direction.y) < 1.0e-4f)
  {
    return;
  }
  const float along = (ship.y - ray.origin.y) / ray.direction.y;
  if (along <= 0.0f)
  {
    return;
  }
  const XMFLOAT3 point{ray.origin.x + ray.direction.x * along, ship.y, ray.origin.z + ray.direction.z * along};
  m_flightInput.GoTo(m_scene.FromRender(point), ORDER_STAND_OFF_POINT_METRES);
}

void TacticalClient::ProcessCommands()
{
  for (const Neuron::CommandEvent& event : m_commands.TakeEvents())
  {
    if (event.phase == Neuron::CommandPhase::Pressed)
    {
      const std::string& name = m_commands.CommandName(event.command);
      if (name == "Dock")
      {
        RequestDock(DockCommand::Dock);
      }
      else if (name == "Launch")
      {
        RequestDock(DockCommand::Launch);
      }
      else if (name == "RepairKit")
      {
        m_pendingItem = ItemKind::RepairKit;
      }
      else if (name == "Target" && m_prediction.Active())
      {
        XMFLOAT3 forward{};
        XMStoreFloat3(&forward, XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), XMLoadFloat4(&m_prediction.State().orientation)));
        m_targeting.Next(TargetCandidates(), m_prediction.State().position, forward);
      }
      else if (name == "ClearTarget")
      {
        m_targeting.Clear();
      }
      else if (name == "TargetPick")
      {
        TargetAtCursor();
      }
      else if (name == "TargetModuleNext" || name == "TargetModulePrevious")
      {
        const ShipState* ship = nullptr;
        for (const ShipState& candidate : m_universe.ships)
        {
          ship = candidate.object == m_targeting.Target() ? &candidate : ship;
        }
        m_targeting.CycleModule(name == "TargetModuleNext" ? 1 : -1, ship != nullptr ? ship->modules.size() : 0);
      }
      else if (name == "Cancel" && m_universe.ownDock.phase != DockPhase::None)
      {
        RequestDock(DockCommand::Cancel);
      }
      else if (name == "Quit")
      {
        m_quitRequested = true;
      }
      else if (name == "Fullscreen")
      {
        m_fullscreenRequested = true;
      }
      else if (name == "Capture")
      {
        m_captureRequested = true;
      }
      else if (name == "Status")
      {
        ToggleStatus();
      }
      else if (name == "Help")
      {
        ToggleHelp();
      }
      else if (name == "ResetView")
      {
        ResetView();
      }
    }
    m_flightInput.OnCommand(event);
  }
  for (Neuron::ControlId click = m_controls.TakeClick(); click != Neuron::NO_CONTROL; click = m_controls.TakeClick())
  {
    DockedSection section = m_section;
    if (DockedView::SectionOf(click, section))
    {
      m_section = section;
    }
    else if (click == DockedView::LAUNCH)
    {
      RequestDock(DockCommand::Launch);
    }
  }
  static_cast<void>(m_controls.TakeRefusal()); // a disabled control shows its reason already
}

void TacticalClient::FollowDock()
{
  if (m_universe.ownDockRevision != m_seenDockRevision)
  {
    // Any statement answers a pending launch: the ship is launching, or the launch was refused.
    m_seenDockRevision = m_universe.ownDockRevision;
    m_launchPending = false;
    m_launchRefused = m_universe.ownDock.answer == DockAnswer::LaunchRefused ? m_universe.ownDock.reason : std::string();
  }
  const bool docked = m_universe.ownDock.phase == DockPhase::Docked;
  if (docked && m_view != ClientView::Tactical)
  {
    SetView(ClientView::Tactical);
    ResetView();
    m_section = DockedSection::Services;
  }
  else if (!docked && m_view == ClientView::Tactical)
  {
    SetView(ClientView::Flight);
  }
}

DockedViewInput TacticalClient::MakeDockedViewInput() const
{
  DockedViewInput input;
  input.width = static_cast<float>(m_device->Width());
  input.height = static_cast<float>(m_device->Height());
  input.scale = HudScaleFor(m_device->Height());
  input.section = m_section;
  const StationDefinition* station = m_system.FindStation(m_universe.ownDock.station);
  input.stationLabel = station != nullptr ? station->label : m_universe.ownDock.station;
  const UniverseObject* object = m_universe.FindObject(m_universe.ownShip);
  const ShipState* ship = object != nullptr ? m_universe.FindShip(object->name) : nullptr;
  input.shipLabel = object != nullptr ? object->name : std::string("the ship");
  if (ship != nullptr && !ship->callsign.empty())
  {
    input.shipLabel += " " + ship->callsign;
  }
  // The server decides (R19); the client offers launch only when the same check would pass.
  const ShipArchetype* archetype = object != nullptr ? FlyingArchetype(m_ships, *object) : nullptr;
  if (archetype == nullptr)
  {
    input.launchAvailable = false;
    input.launchUnavailable = "the ship cannot fly";
  }
  else
  {
    const ShipFlightSpec spec = ShipBodyFor(m_assets.Scene(), m_ships, *archetype, *object).spec;
    input.launchAvailable = spec.engineThrustNewtons > 0.0f && spec.powerCapacity > 0.0f;
    input.launchUnavailable = !(spec.engineThrustNewtons > 0.0f) ? "the ship has no working engine"
                              : !(spec.powerCapacity > 0.0f)     ? "the ship has no power plant"
                                                                 : std::string();
  }
  input.launchPending = m_launchPending;
  input.launchRefused = m_launchRefused;
  input.focus = &m_controls;
  return input;
}

bool TacticalClient::TakeQuitRequest() noexcept
{
  return std::exchange(m_quitRequested, false);
}

bool TacticalClient::TakeFullscreenRequest() noexcept
{
  return std::exchange(m_fullscreenRequested, false);
}

bool TacticalClient::TakeCaptureRequest() noexcept
{
  return std::exchange(m_captureRequested, false);
}

void TacticalClient::ApplyOwnCondition()
{
  if (!m_prediction.Active())
  {
    return;
  }
  const UniverseObject* object = m_universe.FindObject(m_universe.ownShip);
  const ShipState* ship = object != nullptr ? m_universe.FindShip(object->name) : nullptr;
  const HullRecord* hull = object != nullptr ? m_hulls.HullForModel(object->model) : nullptr;
  const ShipArchetype* archetype = object != nullptr ? FlyingArchetype(m_ships, *object) : nullptr;
  if (ship == nullptr || hull == nullptr || archetype == nullptr)
  {
    return;
  }
  const ShipCondition condition = ConditionOf(m_hulls, *hull, ship->modules, m_ships.MountRegions(archetype->name));
  m_prediction.SetSpec(ApplyCondition(m_baseSpec, condition));
}

UniversePosition TacticalClient::AimPoint() const
{
  if (!m_prediction.Active() || m_lastView.viewportSize.x <= 0.0f)
  {
    return m_prediction.State().position;
  }
  const XMMATRIX viewProjection = XMLoadFloat4x4(&m_lastView.view) * XMLoadFloat4x4(&m_lastView.projection);
  const XMFLOAT2 cursor = m_flightInput.Cursor();
  const Neuron::Ray ray =
    Neuron::PickRay(cursor.x, cursor.y, XMMatrixInverse(nullptr, viewProjection), m_lastView.viewportSize.x, m_lastView.viewportSize.y);
  const XMFLOAT3 point{ray.origin.x + ray.direction.x * AIM_DISTANCE_METRES, ray.origin.y + ray.direction.y * AIM_DISTANCE_METRES,
                       ray.origin.z + ray.direction.z * AIM_DISTANCE_METRES};
  return m_scene.FromRender(point);
}

void TacticalClient::ResetChaseCamera(const XMFLOAT4& _orientation)
{
  Neuron::ChaseCameraDesc desc;
  const UniverseObject* object = m_universe.FindObject(m_universe.ownShip);
  const ShipArchetype* archetype = object != nullptr ? FlyingArchetype(m_ships, *object) : nullptr;
  if (const ChaseCameraRecord* record = archetype != nullptr ? m_ships.FindChaseCamera(archetype->name) : nullptr)
  {
    desc.upMetres = record->upMetres;
    desc.backMetres = record->backMetres;
    desc.lookSideDegrees = record->lookSideDegrees;
    desc.lookUpDegrees = record->lookUpDegrees;
    desc.lookDownDegrees = record->lookDownDegrees;
  }
  m_chase.Reset(desc, _orientation);
  m_chaseFor = m_universe.ownShip;
}

void TacticalClient::TakeOwnShipNews(std::uint32_t _ticksPerSecond)
{
  if (m_universe.ownShip == NO_OBJECT)
  {
    return;
  }
  if (m_universe.ownDock.phase != DockPhase::None)
  {
    // The server flies the ship from the Dock command until the launch releases it; its release
    // states the whole flight state again, and prediction starts from that.
    if (m_prediction.Active())
    {
      m_prediction.Stop();
    }
    m_seenOwnShipRevision = m_universe.ownShipRevision;
    return;
  }
  if (m_universe.ownShipRevision == m_seenOwnShipRevision)
  {
    return;
  }
  m_seenOwnShipRevision = m_universe.ownShipRevision;
  if (m_prediction.Active() && m_prediction.Body().object == m_universe.ownShip)
  {
    static_cast<void>(m_prediction.Rebase(m_universe.ownShipTick, m_universe.ownShipState, _ticksPerSecond));
    return;
  }
  // A ship the client's content cannot fly is drawn where the server says, and not predicted.
  const UniverseObject* object = m_universe.FindObject(m_universe.ownShip);
  const ShipArchetype* archetype = object != nullptr ? FlyingArchetype(m_ships, *object) : nullptr;
  if (archetype == nullptr)
  {
    return;
  }
  m_prediction.Begin(ShipBodyFor(m_assets.Scene(), m_ships, *archetype, *object), m_universe.ownShipTick, m_universe.ownShipState);
  m_baseSpec = m_prediction.Body().spec;
  ApplyOwnCondition();
  m_maxBankDegrees = archetype->maxBankDegrees;
  ResetChaseCamera(m_prediction.State().orientation);
}

bool TacticalClient::SendTick(Neuron::LoopbackTransport& _transport, std::uint32_t _ticksPerSecond)
{
  m_ticksPerSecond = _ticksPerSecond;
  ProcessCommands();
  if (m_pendingItem.has_value() && m_universe.ownShip != NO_OBJECT)
  {
    const Message message = UseItemMessage{m_universe.tick, m_universe.ownShip, *m_pendingItem};
    const Route route = RouteOf(KindOf(message));
    m_pendingItem.reset();
    if (!_transport.Send(Neuron::Peer::Client, route.delivery, route.stream, Encode(message)))
    {
      m_protocolError = "an item could not be used";
      return false;
    }
  }
  if (m_pendingDock.has_value() && m_universe.ownShip != NO_OBJECT)
  {
    // A station targeted is where Dock goes (D26); a ship targeted is not a station, so the nearest.
    const ObjectId target = m_targeting.Target();
    const bool station = target != NO_OBJECT && m_universe.FindMotion(target) == nullptr && m_universe.FindObject(target) != nullptr;
    const Message message = DockCommandMessage{m_universe.tick, m_universe.ownShip, *m_pendingDock,
                                               *m_pendingDock == DockCommand::Dock && station ? target : NO_OBJECT};
    const Route route = RouteOf(KindOf(message));
    m_pendingDock.reset();
    if (!_transport.Send(Neuron::Peer::Client, route.delivery, route.stream, Encode(message)))
    {
      m_protocolError = "a docking command could not be sent";
      return false;
    }
  }
  TakeOwnShipNews(_ticksPerSecond);
  FollowDock();
  if (!m_prediction.Active())
  {
    return true;
  }
  if (m_universe.objectsRevision != m_bodiesRevision)
  {
    m_prediction.SetBodies(StaticBodies(m_assets.Scene(), m_ships, m_universe.objects));
    m_bodiesRevision = m_universe.objectsRevision;
  }
  ApplyOwnCondition();
  ShipCommand command = m_flightInput.Sample(m_commands, m_prediction.State(), m_chase.Up(), m_maxBankDegrees, _ticksPerSecond);
  command.aim = AimPoint();
  const ShipTickResult& result = m_prediction.Step(command, _ticksPerSecond);
  if (result.obstructed && command.wishes.cruise)
  {
    m_flightInput.RefuseCruise();
  }
  m_lastCommand = command;
  const Message message = FlightInputMessage{m_prediction.Tick(), m_universe.ownShip, command};
  const Route route = RouteOf(KindOf(message));
  if (!_transport.Send(Neuron::Peer::Client, route.delivery, route.stream, Encode(message)))
  {
    m_protocolError = "the flight input for tick " + std::to_string(m_prediction.Tick()) + " could not be sent";
    return false;
  }
  return true;
}

ViewSetup TacticalClient::TacticalView() const
{
  ViewSetup view;
  const float width = static_cast<float>(m_device->Width());
  const float height = static_cast<float>(m_device->Height());
  // The orbit camera works in world coordinates; render space is the world less the render origin.
  const UniversePosition& origin = m_scene.RenderOrigin();
  const XMMATRIX toWorld = XMMatrixTranslation(static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z));
  XMStoreFloat4x4(&view.view, toWorld * m_camera.View());
  XMStoreFloat4x4(&view.projection, m_camera.Projection(width / std::max(height, 1.0f)));
  const XMFLOAT3 position = m_camera.Position();
  view.position = m_scene.ToRender(UniversePosition{position.x, position.y, position.z});
  view.forward = m_camera.Forward();
  view.viewportSize = XMFLOAT2{width, height};
  view.nearZ = Neuron::OrbitCamera::NEAR_Z;
  return view;
}

ViewSetup TacticalClient::FlightView(const Pose& _ship) const
{
  ViewSetup view;
  const float width = static_cast<float>(m_device->Width());
  const float height = static_cast<float>(m_device->Height());
  const XMFLOAT3 camera = m_scene.ToRender(Offset(_ship.position, m_chase.OffsetFromShip()));
  XMStoreFloat4x4(&view.view, XMMatrixTranslation(-camera.x, -camera.y, -camera.z) * m_chase.View());
  XMStoreFloat4x4(&view.projection, m_chase.Projection(width / std::max(height, 1.0f)));
  view.position = camera;
  view.forward = m_chase.Forward();
  view.viewportSize = XMFLOAT2{width, height};
  view.nearZ = Neuron::ChaseCamera::NEAR_Z;
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
  const Neuron::SideProfile& profile = view->sharedModel->sideProfile;
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
  case InputEvent::Kind::KeyDown:
    OnKey(_event.key, true, false);
    break;
  case InputEvent::Kind::KeyUp:
    OnKey(_event.key, false, false);
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
    ApplyMessage(*message, m_universe);
    static_cast<void>(m_combat.Apply(*message, m_universe));
  }
  return true;
}

void TacticalClient::RebuildSceneIfChanged()
{
  if (m_universe.objectsRevision == m_builtRevision)
  {
    return;
  }
  m_scene.Build(m_assets, m_universe);
  m_builtRevision = m_universe.objectsRevision;
  if (!m_initialSelection.empty() && m_scene.FindInstance(m_initialSelection) != nullptr)
  {
    Select(std::exchange(m_initialSelection, std::string()));
  }
}

std::string TacticalClient::StatusLine(float _framesPerSecond) const
{
  if (!m_showStatus)
  {
    return {};
  }
  char status[256]{};
  std::snprintf(status, sizeof(status), "%ls  |  %zu models, %zu surface voxels  |  %.0f fps", m_device->AdapterName().c_str(),
                m_assets.ModelCount(), m_assets.SurfaceVoxelCount(), static_cast<double>(_framesPerSecond));
  return status;
}

HudInput TacticalClient::MakeHudInput(const ViewSetup& _view, float _timeSeconds, float _framesPerSecond) const
{
  const UniverseSnapshot& snapshot = m_universe;
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
  const XMFLOAT3& target = m_camera.Pose().target;
  const XMFLOAT3 radarCenter = m_scene.ToRender(UniversePosition{target.x, target.y, target.z});
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
    case InstanceView::Kind::Wreck:
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
  input.statusLine = StatusLine(_framesPerSecond);
  return input;
}

FlightHudInput TacticalClient::MakeFlightHudInput(float _timeSeconds, float _framesPerSecond) const
{
  FlightHudInput input;
  input.width = static_cast<float>(m_device->Width());
  input.height = static_cast<float>(m_device->Height());
  input.scale = m_hudScale;
  input.timeSeconds = _timeSeconds;
  for (const ShipState& ship : m_universe.ships)
  {
    if (ship.object == m_universe.ownShip)
    {
      input.callsign = ship.callsign;
    }
  }
  const FlightState& state = m_prediction.State();
  const ShipFlightSpec& spec = m_prediction.Body().spec;
  input.speedMetresPerSecond = state.SpeedMetresPerSecond();
  input.throttle = m_flightInput.Throttle();
  input.powerShare = spec.powerCapacity > 0.0f ? state.powerPool / spec.powerCapacity : 0.0f;
  input.thrustShare = spec.thrustCapacity > 0.0f ? state.thrustPool / spec.thrustCapacity : 0.0f;
  if (spec.shield.capacity > 0.0f)
  {
    input.shieldShare = state.shield / spec.shield.capacity;
    input.shieldUp = state.shieldUp;
  }
  input.target = m_targetReadout;
  if (m_universe.ownDock.phase != DockPhase::None)
  {
    // Carried by the station: the speed the client sees, and the phase in place of an order.
    input.speedMetresPerSecond = m_carriedSpeedMetresPerSecond;
    if (m_prediction.Body().object != m_universe.ownShip)
    {
      // Not flown yet: the pools are full, as the server starts them.
      input.powerShare = 1.0f;
      input.thrustShare = 1.0f;
    }
    input.order = DockPhaseName(m_universe.ownDock.phase);
    std::ranges::transform(input.order, input.order.begin(),
                           [](char _c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(_c))); });
  }
  input.cruise = state.cruise;
  const float chargeTicks = spec.cruiseChargeSeconds * static_cast<float>(m_ticksPerSecond);
  input.cruiseChargeShare = chargeTicks > 0.0f ? static_cast<float>(state.cruiseTimerTicks) / chargeTicks : 0.0f;
  input.cruiseWish = m_flightInput.CruiseWish();
  input.engineOn = m_flightInput.EngineOn();
  input.thruster = state.thrusterActive;
  input.reverse = m_lastCommand.wishes.reverse;
  input.mouseSteering = m_flightInput.MouseSteering();
  input.mouseLocked = m_flightInput.MouseLocked();
  input.cursor = m_flightInput.Cursor();
  input.clickRadiusPixels = input.width * FlightInput::CLICK_RADIUS_SHARE_OF_WIDTH;
  const Manoeuvre& order = m_flightInput.Order();
  if (order.kind == ManoeuvreKind::GoTo && input.order.empty())
  {
    const double dx = order.target.x - state.position.x;
    const double dy = order.target.y - state.position.y;
    const double dz = order.target.z - state.position.z;
    char text[64]{};
    std::snprintf(text, sizeof(text), "GO TO  %.0f M", std::sqrt(dx * dx + dy * dy + dz * dz));
    input.order = text;
  }
  input.hazard = m_hazard;
  input.showHelp = m_showHelp;
  input.statusLine = StatusLine(_framesPerSecond);
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

void TacticalClient::UpdateMaskedModels(const UniversePosition& _camera)
{
  m_combat.Prune(m_universe);
  // Nearest the camera first: what is close is where a stale surface would show (ADR-013).
  std::vector<std::pair<double, ObjectId>> byDistance;
  for (const ClientMask& mask : m_combat.Masks())
  {
    const auto found = std::ranges::find(m_universe.objects, mask.object, &UniverseObject::id);
    if (found == m_universe.objects.end())
    {
      continue;
    }
    const auto index = static_cast<std::size_t>(found - m_universe.objects.begin());
    const UniversePosition& at = index < m_poses.size() ? m_poses[index].position : found->pose.position;
    const double dx = at.x - _camera.x;
    const double dy = at.y - _camera.y;
    const double dz = at.z - _camera.z;
    byDistance.emplace_back(dx * dx + dy * dy + dz * dz, mask.object);
  }
  std::ranges::sort(byDistance);
  std::vector<ObjectId> order;
  order.reserve(byDistance.size());
  for (const auto& entry : byDistance)
  {
    order.push_back(entry.second);
  }
  static_cast<void>(m_surfaces.Update(m_combat, order));

  for (std::size_t i = 0; i < m_maskedModels.size();)
  {
    if (m_combat.Find(m_maskedModels[i].object) == nullptr)
    {
      Retire(std::move(m_maskedModels[i].model->gpu));
      m_maskedModels.erase(m_maskedModels.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    ++i;
  }
  std::vector<ModelOverride> overrides;
  for (const ClientMask& mask : m_combat.Masks())
  {
    const auto held = std::ranges::find(m_maskedModels, mask.object, &MaskedModel::object);
    const MaskedSurface* surface = m_surfaces.Find(mask.object);
    MaskedModel* model = held != m_maskedModels.end() ? &*held : nullptr;
    if (surface != nullptr && surface->ready && (model == nullptr || model->generation != surface->generation))
    {
      // Uploaded before the frame begins: the upload waits for the GPU, and the buffer it replaces
      // is released only once no frame in flight can still read it.
      auto loaded = std::make_unique<LoadedModel>();
      const UniverseObject* object = m_universe.FindObject(mask.object);
      loaded->name = object != nullptr ? object->model : std::string();
      loaded->surface = surface->surface;
      if (!loaded->surface.voxels.empty())
      {
        loaded->gpu = m_renderer.UploadModel(loaded->surface);
        if (loaded->gpu.voxelCount != loaded->surface.voxels.size())
        {
          loaded->gpu = Neuron::GpuVoxelModel{}; // a failed upload draws nothing rather than half
        }
      }
      loaded->glows = BuildGlows(loaded->surface);
      if (model == nullptr)
      {
        m_maskedModels.push_back(MaskedModel{mask.object, surface->generation, std::move(loaded)});
        model = &m_maskedModels.back();
      }
      else
      {
        Retire(std::move(model->model->gpu));
        model->model = std::move(loaded);
        model->generation = surface->generation;
      }
    }
    const UniverseObject* object = m_universe.FindObject(mask.object);
    if (model != nullptr)
    {
      overrides.push_back(ModelOverride{mask.object, model->model.get()});
    }
    else if (object == nullptr || m_assets.Scene().FindInstance(object->name) == nullptr)
    {
      // A wreck has no shared model of its own to show meanwhile.
      overrides.push_back(ModelOverride{mask.object, nullptr});
    }
  }
  m_scene.SetOverrides(std::move(overrides));
}

void TacticalClient::Retire(Neuron::GpuVoxelModel _gpu)
{
  if (_gpu.voxels != nullptr)
  {
    m_retired.push_back(RetiredModel{m_frameCount, std::move(_gpu)});
  }
}

void TacticalClient::ReleaseRetired()
{
  // Each back buffer's frame is waited on before the buffer is used again, so after that many more
  // frames none that read a retired buffer is still in flight.
  std::erase_if(m_retired, [this](const RetiredModel& _retired)
                { return _retired.frame + Neuron::GraphicsDevice::BACK_BUFFER_COUNT + 1 <= m_frameCount; });
}

std::vector<TargetCandidate> TacticalClient::TargetCandidates() const
{
  std::vector<TargetCandidate> candidates;
  for (const InstanceView& view : m_scene.Instances())
  {
    if (view.hidden || view.object == m_universe.ownShip ||
        (view.kind != InstanceView::Kind::Ship && view.kind != InstanceView::Kind::Station))
    {
      continue;
    }
    const UniverseObject* object = m_universe.FindObject(view.object);
    candidates.push_back(TargetCandidate{view.object, object != nullptr ? object->owner : Owner::Neutral, view.pose.position,
                                         view.kind == InstanceView::Kind::Ship});
  }
  return candidates;
}

void TacticalClient::TargetAtCursor()
{
  if (m_lastView.viewportSize.x <= 0.0f)
  {
    return;
  }
  const XMMATRIX viewProjection = XMLoadFloat4x4(&m_lastView.view) * XMLoadFloat4x4(&m_lastView.projection);
  const XMFLOAT2 cursor = m_flightInput.Cursor();
  const Neuron::Ray ray =
    Neuron::PickRay(cursor.x, cursor.y, XMMatrixInverse(nullptr, viewProjection), m_lastView.viewportSize.x, m_lastView.viewportSize.y);
  if (const InstanceView* picked = m_scene.PickTarget(ray); picked != nullptr && picked->object != m_universe.ownShip)
  {
    m_targeting.Set(picked->object);
  }
}

TargetReadout TacticalClient::MakeTargetReadout(const ViewSetup& _view, double _renderTick) const
{
  TargetReadout readout;
  const ObjectId target = m_targeting.Target();
  const InstanceView* view = nullptr;
  for (const InstanceView& candidate : m_scene.Instances())
  {
    view = candidate.object == target ? &candidate : view;
  }
  const UniverseObject* object = m_universe.FindObject(target);
  if (view == nullptr || object == nullptr || !m_prediction.Active())
  {
    return readout;
  }
  readout.active = true;
  readout.hostile = object->owner == Owner::Hostile;
  readout.ship = view->kind == InstanceView::Kind::Ship;
  readout.callsign = object->name;
  for (const ShipState& ship : m_universe.ships)
  {
    if (ship.object != target)
    {
      continue;
    }
    readout.callsign = ship.callsign;
    readout.hullPercent = ship.hullPercent;
    const int module = m_targeting.Module();
    if (module >= 0 && static_cast<std::size_t>(module) < ship.modules.size())
    {
      readout.module = ship.modules[static_cast<std::size_t>(module)].name;
      readout.modulePercent = ship.modules[static_cast<std::size_t>(module)].healthPercent;
    }
  }
  if (const ShipMotionState* motion = m_universe.FindMotion(target))
  {
    readout.shieldPercent = motion->shieldPercent;
  }

  // The box bracketed: the module sub-targeted, or the whole target.
  std::array<XMFLOAT3, 8> corners = Neuron::BoxCorners(view->bounds);
  const HullRecord* hull = m_hulls.HullForModel(object->model);
  const ModuleRegion* region = hull != nullptr && !readout.module.empty() ? hull->FindRegion(readout.module) : nullptr;
  if (region != nullptr)
  {
    const XMMATRIX voxToWorld = XMLoadFloat4x4(&view->voxToWorld);
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
      const XMFLOAT3 vox{static_cast<float>((i & 1u) != 0 ? region->max.x : region->min.x),
                         static_cast<float>((i & 2u) != 0 ? region->max.y : region->min.y),
                         static_cast<float>((i & 4u) != 0 ? region->max.z : region->min.z)};
      XMStoreFloat3(&corners[i], XMVector3TransformCoord(XMLoadFloat3(&vox), voxToWorld));
    }
  }
  XMVECTOR middle = XMVectorZero();
  for (const XMFLOAT3& corner : corners)
  {
    middle = XMVectorAdd(middle, XMLoadFloat3(&corner));
  }
  middle = XMVectorScale(middle, 1.0f / static_cast<float>(corners.size()));
  const XMMATRIX viewProjection = XMLoadFloat4x4(&_view.view) * XMLoadFloat4x4(&_view.projection);
  const Neuron::ProjectedBounds projected = Neuron::ProjectBox(corners, viewProjection, _view.viewportSize.x, _view.viewportSize.y);
  readout.onScreen = projected.visibility == Neuron::ScreenVisibility::OnScreen;
  readout.bracket = projected.rect;

  const XMFLOAT3 own = m_scene.ToRender(m_prediction.State().position);
  readout.distanceMetres = XMVectorGetX(XMVector3Length(XMVectorSubtract(middle, XMLoadFloat3(&own))));

  // The lead (ch. 03 §3.2): where a bolt at the first gun's muzzle speed meets the target, given the
  // target's velocity as drawn and the ship's own, which the bolt carries.
  const ShipFlightSpec& spec = m_prediction.Body().spec;
  float muzzleSpeed = 0.0f;
  for (const LauncherMount& mount : spec.launchers)
  {
    if (mount.fitted && muzzleSpeed == 0.0f && mount.archetype < m_ships.launchers.size())
    {
      muzzleSpeed = m_ships.launchers[mount.archetype].muzzleSpeedMetresPerSecond;
    }
  }
  if (!readout.ship || muzzleSpeed <= 0.0f)
  {
    return readout;
  }
  const Pose now = m_interpolation.At(*object, _renderTick);
  const Pose before = m_interpolation.At(*object, _renderTick - 1.0);
  const auto tps = static_cast<float>(m_ticksPerSecond);
  const XMFLOAT3 targetVelocity{static_cast<float>(now.position.x - before.position.x) * tps,
                                static_cast<float>(now.position.y - before.position.y) * tps,
                                static_cast<float>(now.position.z - before.position.z) * tps};
  XMFLOAT3 offset{};
  XMStoreFloat3(&offset, XMVectorSubtract(middle, XMLoadFloat3(&own)));
  XMFLOAT3 relative{};
  XMStoreFloat3(&relative, XMVectorSubtract(XMLoadFloat3(&targetVelocity), XMLoadFloat3(&m_prediction.State().velocityMetresPerSecond)));
  const std::optional<float> seconds = LeadSeconds(offset, relative, muzzleSpeed);
  if (!seconds.has_value())
  {
    return readout;
  }
  const XMVECTOR lead = XMVectorAdd(middle, XMVectorScale(XMLoadFloat3(&relative), *seconds));
  const XMVECTOR clip = XMVector4Transform(XMVectorSetW(lead, 1.0f), viewProjection);
  const float w = XMVectorGetW(clip);
  if (w > 1.0e-3f)
  {
    readout.lead = XMFLOAT2{(XMVectorGetX(clip) / w * 0.5f + 0.5f) * _view.viewportSize.x,
                            (0.5f - XMVectorGetY(clip) / w * 0.5f) * _view.viewportSize.y};
    readout.leadOnScreen =
      readout.lead.x >= 0.0f && readout.lead.y >= 0.0f && readout.lead.x <= _view.viewportSize.x && readout.lead.y <= _view.viewportSize.y;
  }
  return readout;
}

void TacticalClient::BuildCombatEffects(double _renderTick, float _timeSeconds)
{
  std::vector<Neuron::GpuEffect> effects;
  const auto tps = m_ticksPerSecond;
  // Tracers: a bright head and a beam behind it, the shooter's colour.
  for (const ClientBolt& bolt : m_combat.Bolts())
  {
    const UniverseObject* shooter = m_universe.FindObject(bolt.shooter);
    const bool hostile = shooter != nullptr && shooter->owner == Owner::Hostile;
    const XMFLOAT4 color = hostile ? XMFLOAT4{1.0f, 0.35f, 0.18f, 1.0f} : XMFLOAT4{0.35f, 1.0f, 0.75f, 1.0f};
    const XMFLOAT3 head = m_scene.ToRender(bolt.At(_renderTick, tps));
    const XMFLOAT3 tail{head.x - bolt.velocityMetresPerSecond.x * TRACER_SECONDS, head.y - bolt.velocityMetresPerSecond.y * TRACER_SECONDS,
                        head.z - bolt.velocityMetresPerSecond.z * TRACER_SECONDS};
    effects.push_back(Neuron::GpuEffect{head, 0.5f, tail, 0.15f, color, XMFLOAT4{color.x, color.y, color.z, 0.0f}, 0u, {0.0f, 0.0f, 0.0f}});
    effects.push_back(Neuron::GpuEffect{head,
                                        3.0f,
                                        head,
                                        3.0f,
                                        XMFLOAT4{color.x, color.y, color.z, 0.8f},
                                        XMFLOAT4{1.0f, 1.0f, 1.0f, 0.9f},
                                        Neuron::EFFECT_GLOW | Neuron::EFFECT_WIDTH_IN_PIXELS,
                                        {0.0f, 0.0f, 0.0f}});
  }
  for (const BoltHitMessage& hit : m_combat.TakeHits())
  {
    m_sparks.push_back(Spark{hit.position, _timeSeconds, hit.shield, hit.harmful, hit.bolt});
  }
  std::erase_if(m_sparks, [&](const Spark& _spark) { return _timeSeconds - _spark.startSeconds > EMBER_SECONDS; });
  for (const Spark& spark : m_sparks)
  {
    const float age = _timeSeconds - spark.startSeconds;
    const XMFLOAT3 at = m_scene.ToRender(spark.position);
    if (age < SPARK_SECONDS)
    {
      const float fade = 1.0f - age / SPARK_SECONDS;
      // A shield flashes blue and wide; a hull hit is a hot spark; a hit that does nothing, a dim one.
      const XMFLOAT4 halo = spark.shield ? XMFLOAT4{0.25f, 0.55f, 1.0f, fade} : XMFLOAT4{1.0f, 0.55f, 0.15f, fade};
      const float radius = spark.shield ? 7.0f + 6.0f * (1.0f - fade) : (spark.harmful ? 3.5f : 1.5f);
      effects.push_back(
        Neuron::GpuEffect{at, radius, at, radius, halo, XMFLOAT4{1.0f, 0.95f, 0.85f, fade}, Neuron::EFFECT_GLOW, {0.0f, 0.0f, 0.0f}});
    }
    if (spark.shield || !spark.harmful)
    {
      continue;
    }
    // Embers off a hull hit: a few hot points thrown outward, each its own way from the bolt's number.
    const float fade = 1.0f - age / EMBER_SECONDS;
    for (std::uint32_t i = 0; i < 5; ++i)
    {
      const std::uint32_t hash = Neuron::HashCell(spark.seed, i);
      const float yaw = static_cast<float>(hash & 0xFFFFu) / 65535.0f * XM_2PI;
      const float rise = static_cast<float>(hash >> 16) / 65535.0f * 2.0f - 1.0f;
      const float ring = std::sqrt(std::max(0.0f, 1.0f - rise * rise));
      const float travel = 14.0f * age;
      const XMFLOAT3 ember{at.x + std::cos(yaw) * ring * travel, at.y + rise * travel, at.z + std::sin(yaw) * ring * travel};
      effects.push_back(Neuron::GpuEffect{ember,
                                          2.5f,
                                          ember,
                                          2.5f,
                                          XMFLOAT4{1.0f, 0.45f, 0.1f, fade},
                                          XMFLOAT4{1.0f, 0.85f, 0.5f, fade},
                                          Neuron::EFFECT_GLOW | Neuron::EFFECT_WIDTH_IN_PIXELS,
                                          {0.0f, 0.0f, 0.0f}});
    }
  }
  m_scene.SetCombatEffects(std::move(effects));
}

bool TacticalClient::Render(float _timeSeconds, float _interpolation, bool _capture, bool _vsync, float _framesPerSecond)
{
  const float frameSeconds = m_lastFrameTime < 0.0f ? 0.0f : std::clamp(_timeSeconds - m_lastFrameTime, 0.0f, MAX_FRAME_SECONDS);
  m_lastFrameTime = _timeSeconds;
  ProcessCommands();
  RebuildSceneIfChanged();

  // Every object between the last two ticks it was told of, and the own ship between the last two
  // it predicted: the same fraction of the same tick.
  m_interpolation.Update(m_universe);
  const float alpha = std::clamp(_interpolation, 0.0f, 1.0f);
  const double renderTick = static_cast<double>(m_universe.tick) - 1.0 + static_cast<double>(alpha);
  const bool predicted = m_prediction.Active() && m_prediction.Body().object == m_universe.ownShip;
  m_poses.resize(m_universe.objects.size());
  Pose ship;
  bool shipKnown = false;
  for (std::size_t i = 0; i < m_universe.objects.size(); ++i)
  {
    const UniverseObject& object = m_universe.objects[i];
    m_poses[i] = predicted && object.id == m_universe.ownShip ? m_prediction.Interpolated(alpha) : m_interpolation.At(object, renderTick);
    if (object.id == m_universe.ownShip)
    {
      ship = m_poses[i];
      shipKnown = true;
    }
  }
  // The hazard where the ship is drawn (plan task 2.8). The warning is the client's own, from the
  // zones both halves load (blueprint ch. 01 §3.11); what a hazard does to the ship is the server's.
  m_hazard = shipKnown ? HazardWarningAt(m_system.zones, ship.position, Protects(m_universe.ownDock.phase)) : HazardWarning{};

  // The chase camera follows whether or not it is the view, so that switching back is seamless.
  if (predicted)
  {
    Neuron::ChaseInput chase;
    chase.shipOrientation = ship.orientation;
    chase.shipVelocityMetresPerSecond = m_prediction.State().velocityMetresPerSecond;
    chase.yawThrottle = m_lastCommand.wishes.steering.y;
    chase.pitchThrottle = m_lastCommand.wishes.steering.x;
    chase.frameSeconds = frameSeconds;
    m_chase.Update(chase);
  }
  const bool carried = !predicted && shipKnown && m_universe.ownDock.phase != DockPhase::None;
  if (carried)
  {
    if (m_chaseFor != m_universe.ownShip)
    {
      // A game that starts docked has not flown yet, so the camera is sized on the way out.
      ResetChaseCamera(ship.orientation);
    }
    XMFLOAT3 velocity{};
    if (m_carriedFrom.has_value() && frameSeconds > 0.0f)
    {
      velocity = XMFLOAT3{static_cast<float>((ship.position.x - m_carriedFrom->x) / frameSeconds),
                          static_cast<float>((ship.position.y - m_carriedFrom->y) / frameSeconds),
                          static_cast<float>((ship.position.z - m_carriedFrom->z) / frameSeconds)};
    }
    m_carriedSpeedMetresPerSecond = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
    Neuron::ChaseInput chase;
    chase.shipOrientation = ship.orientation;
    chase.shipVelocityMetresPerSecond = velocity;
    chase.frameSeconds = frameSeconds;
    m_chase.Update(chase);
  }
  m_carriedFrom = carried ? std::optional<UniversePosition>(ship.position) : std::nullopt;
  const bool flying = m_view == ClientView::Flight && (predicted || carried);
  UniversePosition camera;
  if (flying)
  {
    camera = Offset(ship.position, m_chase.OffsetFromShip());
  }
  else
  {
    const XMFLOAT3 position = m_camera.Position();
    camera = UniversePosition{position.x, position.y, position.z};
  }
  UpdateMaskedModels(camera);
  m_scene.Update(m_universe, m_poses, camera);
  const ViewSetup view = flying ? FlightView(ship) : TacticalView();
  m_lastView = view;
  BuildCombatEffects(renderTick, _timeSeconds);
  m_targeting.Keep(TargetCandidates());
  m_targetReadout = flying ? MakeTargetReadout(view, renderTick) : TargetReadout{};
  ReleaseRetired();
  ++m_frameCount;

  if (m_device->BeginFrame() == nullptr)
  {
    return false;
  }
  m_renderer.ClearMainTargets(CLEAR_COLOR);
  // Ships move, so the key light's shadow map is drawn every frame (ADR-010).
  m_scene.RenderShadows(m_renderer);
  m_device->Timestamp();

  m_renderer.BeginPass(m_scene.MakeFrame(view, _timeSeconds, true));
  const InstanceView* selected = flying || m_selected.empty() ? nullptr : m_scene.FindInstance(m_selected);
  m_scene.Render(m_renderer, view, selected, _timeSeconds);
  m_device->Timestamp();

  m_batch.Begin(&m_fonts, &m_sprites);
  Neuron::FrameConstants hudFrame{};
  hudFrame.viewportSize = view.viewportSize;
  hudFrame.exposure = 1.0f;
  if (flying)
  {
    static_cast<void>(m_flightHud.Build(m_batch, MakeFlightHudInput(_timeSeconds, _framesPerSecond)));
    m_renderer.BeginPass(hudFrame);
    m_renderer.DrawHud(m_batch.Vertices(Neuron::HudBatch::BACK));
  }
  else
  {
    const HudInput input = MakeHudInput(view, _timeSeconds, _framesPerSecond);
    const HudLayout layout = m_hud.Build(m_batch, input);
    m_renderer.BeginPass(hudFrame);
    m_renderer.DrawHud(m_batch.Vertices(Neuron::HudBatch::BACK));
    RenderThumbnails(layout, _timeSeconds);
    RenderSilhouette(layout, _timeSeconds);
  }
  m_controls.BeginLayout();
  if (m_view == ClientView::Tactical)
  {
    m_batch.SetLayer(Neuron::HudBatch::FRONT);
    for (const DockedControl& control : m_dockedView.Build(m_batch, MakeDockedViewInput()).controls)
    {
      m_controls.Add(control.id, control.rect, control.enabled, control.reason);
    }
  }
  m_renderer.BeginPass(hudFrame);
  m_renderer.DrawHud(m_batch.Vertices(Neuron::HudBatch::FRONT));
  return m_device->EndFrame(_capture, _vsync);
}

} // namespace Outpost
