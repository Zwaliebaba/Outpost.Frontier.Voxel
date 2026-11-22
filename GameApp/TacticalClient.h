#pragma once

#include "DockedView.h"
#include "ChaseCamera.h"
#include "CommandMap.h"
#include "ControlFocus.h"
#include "FlightHud.h"
#include "FlightInput.h"
#include "FontAtlas.h"
#include "GraphicsDevice.h"
#include "HudBatch.h"
#include "InputScript.h"
#include "LoopbackTransport.h"
#include "OrbitCamera.h"
#include "PointerGesture.h"
#include "Prediction.h"
#include "Renderer.h"
#include "SceneAssets.h"
#include "ShipArchetype.h"
#include "SystemContent.h"
#include "SpriteSheet.h"
#include "TacticalHud.h"
#include "TacticalScene.h"
#include "UniverseSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Outpost
{

/// Which view the client draws: the chase camera behind the player's ship, or the tactical view of
/// the whole yard (Design/GameDesign.md §3.1; plan tasks 1.8 and 1.10).
enum class ClientView : std::uint8_t
{
  Flight,
  Tactical
};

/// The client half of the game as far as it is built. It loads the scene's assets and the
/// bindings, owns the cameras, the input layer and the prediction of its own ship, builds its own
/// UniverseSnapshot from the messages the server sends, and draws that; it never reads the server's
/// Universe (AGENTS.md R18, R25). What it asks of the server it sends as a FlightInputMessage a tick.
class TacticalClient
{
public:
  // The GPU timestamps a frame writes, as indices into GraphicsDevice::CompletedTimestampsMilliseconds():
  // the frame's start, the end of the shadow pass, the end of the scene's voxels and effects, and the
  // end of the frame after the HUD.
  static constexpr std::size_t TIMESTAMP_SHADOWS_END = 1;
  static constexpr std::size_t TIMESTAMP_SCENE_END = 2;
  static constexpr std::size_t TIMESTAMP_FRAME_END = 3;
  // A go-to order on a ship stops this many of its bounding radii short of it.
  static constexpr float ORDER_STAND_OFF_RADII = 3.0f;
  static constexpr float ORDER_STAND_OFF_POINT_METRES = 10.0f;

  TacticalClient() = default;
  /// Waits for the GPU before any of the client's resources is released (see the .cpp).
  ~TacticalClient();
  TacticalClient(const TacticalClient&) = delete;
  TacticalClient& operator=(const TacticalClient&) = delete;
  TacticalClient(TacticalClient&&) = delete;
  TacticalClient& operator=(TacticalClient&&) = delete;

  [[nodiscard]] bool Initialize(Neuron::GraphicsDevice& _device, const std::filesystem::path& _assetRoot, std::string& _outError);

  /// Call after the device has resized its swap chain.
  [[nodiscard]] bool Resize(std::uint32_t _width, std::uint32_t _height);

  void OnPointerDown(PointerButton _button, float _x, float _y, std::uint64_t _timeMilliseconds);
  void OnPointerMove(float _x, float _y);
  void OnPointerUp(PointerButton _button, float _x, float _y, std::uint64_t _timeMilliseconds);
  void OnPointerCancel();
  void OnWheel(float _steps);
  /// A keyboard key, as its Win32 virtual-key code; _repeat for the keyboard's auto-repeat.
  void OnKey(Neuron::InputKey _key, bool _down, bool _repeat);
  /// The window lost the focus: every held key is released.
  void OnFocusLost();
  /// Plays one scripted input as if it came from the window at that time (plan task F5).
  void Apply(const InputEvent& _event, std::uint64_t _timeMilliseconds);
  void ResetView();
  void ToggleHelp() noexcept
  {
    m_showHelp = !m_showHelp;
  }
  void ToggleStatus() noexcept
  {
    m_showStatus = !m_showStatus;
  }
  void SetView(ClientView _view);
  [[nodiscard]] ClientView View() const noexcept
  {
    return m_view;
  }

  /// Selects the ship drawn by a scene instance, or clears the selection for an empty name.
  void Select(std::string_view _instance);
  [[nodiscard]] const std::string& Selected() const noexcept
  {
    return m_selected;
  }

  /// Before the server runs its next tick: samples the input, flies the tick ahead of the server
  /// and sends the command for it. False, with ProtocolError() saying why, if it could not be sent.
  [[nodiscard]] bool SendTick(Neuron::LoopbackTransport& _transport, std::uint32_t _ticksPerSecond);
  /// Asks the server to dock, cancel or launch; sent with the next tick. While the server says the
  /// ship is docking, docked or launching, it flies the ship and the client draws it where it says.
  void RequestDock(DockCommand _command)
  {
    m_pendingDock = _command;
    if (_command == DockCommand::Launch && m_universe.ownDock.phase == DockPhase::Docked)
    {
      m_launchPending = true;
      m_launchRefused.clear();
    }
  }
  /// The docked view's section and launch state, for tests and the smoke report.
  [[nodiscard]] DockedSection Section() const noexcept
  {
    return m_section;
  }
  [[nodiscard]] const std::string& LaunchRefusal() const noexcept
  {
    return m_launchRefused;
  }

  /// Decodes every message waiting for the client and applies it to the client's universe. False,
  /// with ProtocolError() saying why, if a message did not decode: a defect, never a normal event.
  [[nodiscard]] bool Receive(Neuron::LoopbackTransport& _transport);
  [[nodiscard]] const std::string& ProtocolError() const noexcept
  {
    return m_protocolError;
  }
  /// The universe as the client has been told it.
  [[nodiscard]] const UniverseSnapshot& Universe() const noexcept
  {
    return m_universe;
  }
  [[nodiscard]] const Prediction& OwnShip() const noexcept
  {
    return m_prediction;
  }
  [[nodiscard]] const FlightInput& Input() const noexcept
  {
    return m_flightInput;
  }

  /// Records and submits one frame, drawn _interpolation of the way from the previous tick to the
  /// last one. Returns false when the device was lost.
  [[nodiscard]] bool Render(float _timeSeconds, float _interpolation, bool _capture, bool _vsync, float _framesPerSecond);

  /// Requests the window acts on, each true once after its command.
  [[nodiscard]] bool TakeQuitRequest() noexcept;
  [[nodiscard]] bool TakeFullscreenRequest() noexcept;
  [[nodiscard]] bool TakeCaptureRequest() noexcept;
  /// The chase view keeps the cursor inside the window, where it steers.
  [[nodiscard]] bool WantsCursorConfined() const noexcept
  {
    return m_view == ClientView::Flight && m_prediction.Active();
  }

  [[nodiscard]] const SceneDefinition& Scene() const noexcept
  {
    return m_assets.Scene();
  }
  [[nodiscard]] const SceneAssets& Assets() const noexcept
  {
    return m_assets;
  }
  [[nodiscard]] const std::vector<std::string>& Warnings() const noexcept
  {
    return m_assets.Warnings();
  }

private:
  [[nodiscard]] ViewSetup TacticalView() const;
  [[nodiscard]] ViewSetup FlightView(const Pose& _ship) const;
  [[nodiscard]] bool RebuildFonts(float _scale);
  void BuildSilhouette();
  void RenderThumbnails(const HudLayout& _layout, float _timeSeconds);
  void RenderSilhouette(const HudLayout& _layout, float _timeSeconds);
  [[nodiscard]] HudInput MakeHudInput(const ViewSetup& _view, float _timeSeconds, float _framesPerSecond) const;
  [[nodiscard]] FlightHudInput MakeFlightHudInput(float _timeSeconds, float _framesPerSecond) const;
  [[nodiscard]] std::string StatusLine(float _framesPerSecond) const;
  void RebuildSceneIfChanged();
  void TakeOwnShipNews(std::uint32_t _ticksPerSecond);
  /// The view follows the dock (D10): the tactical view while docked and only then, framed on the
  /// station with the services open; the flight view from the launch on.
  void FollowDock();
  void ProcessCommands();
  [[nodiscard]] DockedViewInput MakeDockedViewInput() const;
  void OrderAt(float _x, float _y);
  /// Sizes the chase camera for the own ship's archetype and points it along _orientation.
  void ResetChaseCamera(const DirectX::XMFLOAT4& _orientation);

  Neuron::GraphicsDevice* m_device = nullptr;
  Neuron::Renderer m_renderer;
  SceneAssets m_assets;
  ShipContent m_ships;
  TacticalScene m_scene;
  TacticalHud m_hud;
  FlightHud m_flightHud;
  Neuron::FontAtlas m_fonts;
  Neuron::SpriteSheet m_sprites;
  Neuron::HudBatch m_batch;
  Neuron::OrbitCamera m_camera;
  Neuron::ChaseCamera m_chase;
  Neuron::PointerGesture m_gesture;
  Neuron::PointerGesture m_orderGesture;
  Neuron::CommandMap m_commands;
  FlightInput m_flightInput;
  Prediction m_prediction;
  PoseInterpolation m_interpolation;
  ShipCommand m_lastCommand;
  ViewSetup m_lastView;
  ClientView m_view = ClientView::Flight;
  bool m_panning = false;
  float m_lastX = 0.0f;
  float m_lastY = 0.0f;
  UniverseSnapshot m_universe;
  std::uint64_t m_builtRevision = 0;
  std::uint64_t m_bodiesRevision = ~0ull;
  std::uint64_t m_seenOwnShipRevision = 0;
  std::optional<DockCommand> m_pendingDock; // asked by a key or the services panel, sent with the next tick
  std::uint64_t m_seenDockRevision = 0;
  SystemContent m_system; // the stations' names, for the docked view
  DockedView m_dockedView;
  Neuron::ControlFocus m_controls;
  DockedSection m_section = DockedSection::Services;
  bool m_uiPress = false;       // the left press went to a control, not the view
  bool m_launchPending = false; // asked, and the server has not stated anything since
  std::string m_launchRefused;  // the server's last refusal, until the next request
  float m_maxBankDegrees = 0.0f;
  std::uint32_t m_ticksPerSecond = 60;
  float m_lastFrameTime = -1.0f;
  // While the server carries the own ship (docking or launching), the chase camera follows the pose
  // the server states, and its speed is measured from one frame to the next.
  std::optional<UniversePosition> m_carriedFrom;
  float m_carriedSpeedMetresPerSecond = 0.0f;
  ObjectId m_chaseFor = NO_OBJECT; // the ship the chase camera was sized for
  std::string m_protocolError;
  std::string m_initialSelection; // applied once the selected ship's object has arrived
  std::string m_selected;
  std::string m_silhouetteFor;
  std::vector<Neuron::GpuLineSegment> m_silhouette; // model space of the selected ship
  DirectX::XMFLOAT3 m_silhouetteMin{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 m_silhouetteMax{0.0f, 0.0f, 0.0f};
  std::vector<Pose> m_poses; // this frame's pose for each snapshot object
  float m_hudScale = 0.0f;
  bool m_showHelp = true;
  bool m_showStatus = true;
  bool m_quitRequested = false;
  bool m_fullscreenRequested = false;
  bool m_captureRequested = false;
};

} // namespace Outpost
