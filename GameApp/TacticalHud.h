#pragma once

#include "FontAtlas.h"
#include "HudBatch.h"
#include "ScreenProjection.h"
#include "UniverseSnapshot.h"

#include <string>
#include <vector>

namespace Outpost
{

/// Font slots, in the order TacticalHud::Fonts() returns them.
enum HudFont : std::size_t
{
  FontSmall = 0,
  FontBody,
  FontBodyStrong,
  FontHeading,
  FontTitle,
  FontTiny,
  HudFontCount
};

/// A ship as the HUD anchors to it: projected bounds plus what the bar shows.
struct ShipMarker
{
  std::string callsign;
  Neuron::ProjectedBounds bounds;
  int hullPercent = 100;
  bool selected = false;
};

struct RadarBlip
{
  enum class Kind : std::uint8_t
  {
    Ship,
    SelectedShip,
    Station,
    Rock,
    Blueprint
  };

  float x = 0.0f; // world X relative to the radar centre
  float z = 0.0f; // world Z relative to the radar centre
  float sizeWorld = 0.0f;
  Kind kind = Kind::Station;
};

struct HudInput
{
  float width = 1920.0f;
  float height = 1080.0f;
  float scale = 1.0f; // 1 at 1080 lines
  float timeSeconds = 0.0f;
  const UniverseSnapshot* snapshot = nullptr;
  const ShipState* selected = nullptr;
  std::vector<ShipMarker> markers;
  std::vector<RadarBlip> blips;
  float radarRangeWorld = 600.0f;
  float cameraYawDegrees = 0.0f;
  float cameraFovDegrees = 40.0f;
  std::vector<std::string> buildLabels;
  bool showHelp = true;
  std::string statusLine; // bottom-right diagnostics, empty to hide
};

/// Where the HUD wants the client to draw 3D content between its back and front layers.
struct HudLayout
{
  std::vector<Neuron::ScreenRect> buildSlots;
  Neuron::ScreenRect silhouette{0.0f, 0.0f, 0.0f, 0.0f};
};

/// The military command-and-control overlay: title, objectives, resources, radar, the
/// selected-ship panel and the build panel, plus brackets and health bars anchored to ships.
/// Pure layout over a HudBatch; every number on it comes from the snapshot or the scene.
class TacticalHud
{
public:
  [[nodiscard]] static std::vector<Neuron::FontSpec> Fonts(float _scale);

  [[nodiscard]] HudLayout Build(Neuron::HudBatch& _batch, const HudInput& _input) const;

private:
  void DrawMarkers(Neuron::HudBatch& _batch, const HudInput& _input) const;
  void DrawTitle(Neuron::HudBatch& _batch, const HudInput& _input) const;
  void DrawObjectives(Neuron::HudBatch& _batch, const HudInput& _input) const;
  void DrawResources(Neuron::HudBatch& _batch, const HudInput& _input) const;
  void DrawRadar(Neuron::HudBatch& _batch, const HudInput& _input) const;
  void DrawShipPanel(Neuron::HudBatch& _batch, const HudInput& _input, HudLayout& _layout) const;
  void DrawBuildPanel(Neuron::HudBatch& _batch, const HudInput& _input, HudLayout& _layout) const;
};

} // namespace Outpost
