#pragma once

#include "FontAtlas.h"
#include "GraphicsDevice.h"
#include "HudBatch.h"
#include "OrbitCamera.h"
#include "PointerGesture.h"
#include "Renderer.h"
#include "SceneAssets.h"
#include "TacticalHud.h"
#include "TacticalScene.h"
#include "WorldSnapshot.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Outpost
{

enum class PointerButton : std::uint8_t
{
  Left,
  Right,
  Middle
};

/// The client half of the sample: it loads the scene's assets, owns the camera and the selection,
/// and draws a WorldSnapshot — it never reads the server's World directly.
class TacticalClient
{
public:
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
  void ResetView();
  void ToggleHelp() noexcept
  {
    m_showHelp = !m_showHelp;
  }
  void ToggleStatus() noexcept
  {
    m_showStatus = !m_showStatus;
  }

  /// Selects the ship drawn by a scene instance, or clears the selection for an empty name.
  void Select(std::string_view _instance);
  [[nodiscard]] const std::string& Selected() const noexcept
  {
    return m_selected;
  }

  /// Records and submits one frame. Returns false when the device was lost.
  [[nodiscard]] bool Render(const WorldSnapshot& _snapshot, float _timeSeconds, bool _capture, bool _vsync, float _framesPerSecond);

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
  [[nodiscard]] HudInput MakeHudInput(const WorldSnapshot& _snapshot, const ViewSetup& _view, float _timeSeconds,
                                      float _framesPerSecond) const;

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
  std::string m_selected;
  std::string m_silhouetteFor;
  std::vector<Neuron::GpuLineSegment> m_silhouette; // model space of the selected ship
  DirectX::XMFLOAT3 m_silhouetteMin{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 m_silhouetteMax{0.0f, 0.0f, 0.0f};
  float m_hudScale = 0.0f;
  bool m_shadowDirty = true;
  bool m_showHelp = true;
  bool m_showStatus = true;
};

} // namespace Outpost
