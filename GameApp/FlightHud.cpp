#include "FlightHud.h"

#include "HudPalette.h"
#include "TacticalHud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Outpost
{

using Neuron::HudBatch;
using Neuron::Rgba;

namespace
{

constexpr float PANEL_WIDTH = 520.0f;
constexpr float PANEL_HEIGHT = 118.0f;
constexpr float BAR_HEIGHT = 10.0f;

[[nodiscard]] Neuron::ScreenRect Rect(float _x, float _y, float _width, float _height) noexcept
{
  return Neuron::ScreenRect{_x, _y, _x + _width, _y + _height};
}

/// A labelled horizontal bar filled to a share.
void Bar(HudBatch& _batch, const Neuron::ScreenRect& _rect, float _share, std::uint32_t _color, const char* _label, float _scale)
{
  const float width = _rect.right - _rect.left;
  const float height = _rect.bottom - _rect.top;
  _batch.Rect(_rect.left, _rect.top, width, height, BAR_TRACK);
  _batch.Rect(_rect.left, _rect.top, width * std::clamp(_share, 0.0f, 1.0f), height, _color);
  _batch.Text(FontTiny, _rect.left, _rect.top - 15.0f * _scale, _label, TEXT_DIM);
}

[[nodiscard]] const char* CruiseText(const FlightHudInput& _input) noexcept
{
  switch (_input.cruise)
  {
  case CruiseState::Charging:
    return "CRUISE CHARGING";
  case CruiseState::Cruising:
    return "CRUISE";
  case CruiseState::Cooldown:
    return "CRUISE COOLDOWN";
  default:
    return _input.cruiseWish ? "CRUISE BLOCKED" : "";
  }
}

} // namespace

FlightHudLayout FlightHud::Build(HudBatch& _batch, const FlightHudInput& _input) const
{
  FlightHudLayout layout;
  const float s = _input.scale;
  const float centerX = _input.width * 0.5f;
  const float centerY = _input.height * 0.5f;

  // The reticle: where the nose points.
  const float reticle = 14.0f * s;
  _batch.Circle(centerX, centerY, reticle, std::max(1.0f, 1.5f * s), ACCENT_SOFT);
  _batch.Line(centerX - reticle * 1.8f, centerY, centerX - reticle * 0.8f, centerY, std::max(1.0f, 1.5f * s), ACCENT);
  _batch.Line(centerX + reticle * 0.8f, centerY, centerX + reticle * 1.8f, centerY, std::max(1.0f, 1.5f * s), ACCENT);
  _batch.Line(centerX, centerY + reticle * 0.8f, centerX, centerY + reticle * 1.4f, std::max(1.0f, 1.5f * s), ACCENT);

  // The cursor while it steers: a line from the reticle, the farther the harder the turn.
  if (_input.mouseSteering)
  {
    // The ring stays whole on screen when the cursor is at an edge.
    const float ring = 6.0f * s;
    const float x = std::clamp(_input.cursor.x, ring, _input.width - ring);
    const float y = std::clamp(_input.cursor.y, ring, _input.height - ring);
    _batch.Line(centerX, centerY, x, y, std::max(1.0f, 1.2f * s), ACCENT_SOFT);
    _batch.Circle(x, y, ring, std::max(1.0f, 1.5f * s), _input.mouseLocked ? AMBER : ACCENT, 24);
  }

  // The flight panel at the bottom centre.
  const float panelWidth = std::min(PANEL_WIDTH * s, _input.width - 24.0f * s);
  const float panelHeight = PANEL_HEIGHT * s;
  const float panelX = centerX - panelWidth * 0.5f;
  const float panelY = _input.height - panelHeight - 40.0f * s;
  layout.panel = Rect(panelX, panelY, panelWidth, panelHeight);
  _batch.Rect(panelX, panelY, panelWidth, panelHeight, PANEL_FILL);
  _batch.Outline(panelX, panelY, panelWidth, panelHeight, std::max(1.0f, 1.2f * s), PANEL_EDGE_DIM);
  _batch.Rect(panelX, panelY, panelWidth, 3.0f * s, ACCENT);

  char text[64]{};
  std::snprintf(text, sizeof(text), "%.0f", static_cast<double>(_input.speedMetresPerSecond));
  const float speedX = panelX + 18.0f * s;
  _batch.Text(FontTitle, speedX, panelY + 14.0f * s, text, TEXT);
  _batch.Text(FontSmall, speedX, panelY + 50.0f * s, "M/S", TEXT_DIM);
  _batch.Text(FontBodyStrong, speedX, panelY + 76.0f * s, _input.callsign, ACCENT);

  const float barsX = panelX + 150.0f * s;
  const float barsWidth = panelWidth - 150.0f * s - 18.0f * s;
  layout.throttleBar = Rect(barsX, panelY + 30.0f * s, barsWidth, BAR_HEIGHT * s);
  layout.powerBar = Rect(barsX, panelY + 62.0f * s, barsWidth * 0.48f, BAR_HEIGHT * s);
  layout.thrustBar = Rect(barsX + barsWidth * 0.52f, panelY + 62.0f * s, barsWidth * 0.48f, BAR_HEIGHT * s);
  Bar(_batch, layout.throttleBar, _input.throttle, _input.reverse ? AMBER : ACCENT, _input.reverse ? "REVERSE" : "THROTTLE", s);
  Bar(_batch, layout.powerBar, _input.powerShare, GREEN, "POWER", s);
  Bar(_batch, layout.thrustBar, _input.thrustShare, _input.thruster ? AMBER : GREEN, "THRUST", s);

  // The engine's state, on one line under the bars.
  const float stateY = panelY + 84.0f * s;
  float stateX = barsX;
  const auto state = [&](const char* _label, std::uint32_t _color)
  {
    if (_label[0] != '\0')
    {
      stateX += _batch.Text(FontSmall, stateX, stateY, _label, _color) + 16.0f * s;
    }
  };
  if (!_input.engineOn)
  {
    state("ENGINE KILLED", RED);
  }
  if (_input.thruster)
  {
    state("THRUSTER", AMBER);
  }
  state(CruiseText(_input), _input.cruise == CruiseState::Cruising ? ACCENT : AMBER);
  if (_input.cruise == CruiseState::Charging)
  {
    _batch.Rect(barsX, stateY + 18.0f * s, barsWidth * std::clamp(_input.cruiseChargeShare, 0.0f, 1.0f), 2.0f * s, AMBER);
  }
  if (!_input.order.empty())
  {
    state(_input.order.c_str(), ACCENT);
  }

  if (_input.showHelp)
  {
    const char* help = "Hold LMB: steer   Space: mouse lock   W/S, wheel: throttle   Tab: thruster   C: cruise   Z: engine   "
                       "A/D/R/F: strafe   Q/E: roll   G: dock   Esc: cancel   H: help   F10: quit";
    const float helpWidth = _batch.TextWidth(FontSmall, help);
    const float helpY = _input.height - 26.0f * s;
    layout.help = Rect(centerX - helpWidth * 0.5f, helpY, helpWidth, _batch.LineHeight(FontSmall));
    _batch.Text(FontSmall, centerX, helpY, help, TEXT_DIM, Neuron::TextAlign::Center);
  }
  if (!_input.statusLine.empty())
  {
    _batch.Text(FontTiny, centerX, 10.0f * s, _input.statusLine, TEXT_DIM, Neuron::TextAlign::Center);
  }
  return layout;
}

} // namespace Outpost
