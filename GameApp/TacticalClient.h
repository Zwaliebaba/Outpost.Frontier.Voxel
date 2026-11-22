#pragma once

#include "FontAtlas.h"
#include "GraphicsDevice.h"
#include "HudBatch.h"
#include "InputScript.h"
#include "LoopbackTransport.h"
#include "OrbitCamera.h"
#include "PointerGesture.h"
#include "Renderer.h"
#include "SceneAssets.h"
#include "TacticalHud.h"
#include "TacticalScene.h"
#include "WorldSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Outpost
{

/// The client half of the game as far as it is built: the design's tactical view
/// (Design/GameDesign.md §3.1). It loads the scene's assets, owns the camera and the selection,
/// builds its own WorldSnapshot from the messages the server sends, and draws that — it never
/// reads the server's World (AGENTS.md R18, R25).
class TacticalClient
{
public:
  // The GPU timestamps a frame writes, as indices into GraphicsDevice::CompletedTimestampsMilliseconds():
  // the frame's start, the end of the shadow pass, the end of the scene's voxels and effects, and the
  // end of the frame after the HUD.
  static constexpr std::size_t TIMESTAMP_SHADOWS_END = 1;
  static constexpr std::size_t TIMESTAMP_SCENE_END = 2;
  static constexpr std::size_t TIMESTAMP_FRAME_END = 3;

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
  /// Draws the shadow map every frame instead of once, as moving ships will need (plan task F6).
  void SetShadowsEveryFrame(bool _everyFrame) noexcept
  {
    m_shadowsEveryFrame = _everyFrame;
  }

  /// Selects the ship drawn by a scene instance, or clears the selection for an empty name.
  void Select(std::string_view _instance);
  [[nodiscard]] const std::string& Selected() const noexcept
  {
    return m_selected;
  }

  /// Decodes every message waiting for the client and applies it to the client's world. False,
  /// with ProtocolError() saying why, if a message did not decode: a defect, never a normal event.
  [[nodiscard]] bool Receive(Neuron::LoopbackTransport& _transport);
  [[nodiscard]] const std::string& ProtocolError() const noexcept
  {
    return m_protocolError;
  }
  /// The world as the client has been told it.
  [[nodiscard]] const WorldSnapshot& World() const noexcept
  {
    return m_world;
  }

  /// Records and submits one frame. Returns false when the device was lost.
  [[nodiscard]] bool Render(float _timeSeconds, bool _capture, bool _vsync, float _framesPerSecond);

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
  [[nodiscard]] ViewSetup MainView() const;
  [[nodiscard]] bool RebuildFonts(float _scale);
  void BuildSilhouette();
  void RenderThumbnails(const HudLayout& _layout, float _timeSeconds);
  void RenderSilhouette(const HudLayout& _layout, float _timeSeconds);
  [[nodiscard]] HudInput MakeHudInput(const ViewSetup& _view, float _timeSeconds, float _framesPerSecond) const;
  void RebuildSceneIfChanged();

  Neuron::GraphicsDevice* m_device = nullptr;
  Neuron::Renderer m_renderer;
  SceneAssets m_assets;
  TacticalScene m_scene;
  TacticalHud m_hud;
  Neuron::FontAtlas m_fonts;
  Neuron::HudBatch m_batch;
  Neuron::OrbitCamera m_camera;
  Neuron::PointerGesture m_gesture;
  bool m_panning = false;
  float m_lastX = 0.0f;
  float m_lastY = 0.0f;
  WorldSnapshot m_world;
  std::uint64_t m_builtRevision = 0;
  std::string m_protocolError;
  std::string m_initialSelection; // applied once the selected ship's object has arrived
  std::string m_selected;
  std::string m_silhouetteFor;
  std::vector<Neuron::GpuLineSegment> m_silhouette; // model space of the selected ship
  DirectX::XMFLOAT3 m_silhouetteMin{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 m_silhouetteMax{0.0f, 0.0f, 0.0f};
  float m_hudScale = 0.0f;
  bool m_shadowDirty = true;
  bool m_shadowsEveryFrame = false;
  bool m_showHelp = true;
  bool m_showStatus = true;
};

} // namespace Outpost
