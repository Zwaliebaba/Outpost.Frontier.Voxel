#include "FlightHud.h"

#include "HudPalette.h"
#include "TacticalHud.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace Outpost
{

using Neuron::HudBatch;
using Neuron::Rgba;

namespace
{

constexpr float PANEL_WIDTH = 520.0f;
constexpr float PANEL_HEIGHT = 118.0f;
constexpr float BAR_HEIGHT = 10.0f;
constexpr float WARNING_WIDTH = 340.0f;
constexpr float WARNING_HEIGHT = 62.0f;
constexpr float WARNING_TOP = 84.0f;
constexpr float WARNING_PULSES_PER_SECOND = 1.0f;
constexpr float TARGET_PANEL_WIDTH = 300.0f;
constexpr float TARGET_PANEL_HEIGHT = 124.0f;
constexpr float TARGET_PANEL_MARGIN = 24.0f;

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

/// The hazard warning, top centre: red for a zone that damages, amber for one that only blinds. Its
/// frame pulses; the words do not, so they stay readable.
[[nodiscard]] Neuron::ScreenRect Warning(HudBatch& _batch, const FlightHudInput& _input)
{
  const float s = _input.scale;
  const HazardWarning& hazard = _input.hazard;
  const std::uint32_t color = hazard.damaging ? RED : AMBER;
  const float width = std::min(WARNING_WIDTH * s, _input.width - 24.0f * s);
  const float height = WARNING_HEIGHT * s;
  const float x = _input.width * 0.5f - width * 0.5f;
  const float y = WARNING_TOP * s;
  const float pulse = 0.5f + 0.5f * std::sin(_input.timeSeconds * WARNING_PULSES_PER_SECOND * 2.0f * std::numbers::pi_v<float>);
  _batch.Rect(x, y, width, height, PANEL_FILL);
  _batch.Outline(x, y, width, height, std::max(1.0f, 1.5f * s), Neuron::FadeColor(color, 0.35f + 0.65f * pulse));
  _batch.Rect(x, y, width, 3.0f * s, color);
  _batch.Text(FontBodyStrong, _input.width * 0.5f, y + 10.0f * s, "HAZARD ZONE", color, Neuron::TextAlign::Center);

  std::string effects = hazard.damaging ? "DANGER TO HULL" : "";
  if (hazard.scannerLossPercent > 0)
  {
    char scanner[32]{};
    std::snprintf(scanner, sizeof(scanner), "SCANNER -%d %%", hazard.scannerLossPercent);
    effects += effects.empty() ? "" : "   ";
    effects += scanner;
  }
  _batch.Text(FontSmall, _input.width * 0.5f, y + 36.0f * s, effects, TEXT, Neuron::TextAlign::Center);
  return Rect(x, y, width, height);
}

/// Corner brackets around the target on screen, and its lead marker.
void Brackets(HudBatch& _batch, const FlightHudInput& _input)
{
  const float s = _input.scale;
  const TargetReadout& target = _input.target;
  const std::uint32_t color = target.hostile ? RED : ACCENT;
  const float thickness = std::max(1.0f, 1.8f * s);
  if (target.onScreen)
  {
    // At least a little box, so a far target is still bracketed.
    const float half = 10.0f * s;
    const float centerX = (target.bracket.left + target.bracket.right) * 0.5f;
    const float centerY = (target.bracket.top + target.bracket.bottom) * 0.5f;
    const float left = std::min(target.bracket.left, centerX - half);
    const float right = std::max(target.bracket.right, centerX + half);
    const float top = std::min(target.bracket.top, centerY - half);
    const float bottom = std::max(target.bracket.bottom, centerY + half);
    const float arm = std::min({12.0f * s, (right - left) * 0.4f, (bottom - top) * 0.4f});
    for (const auto& [x, y, dx, dy] : {std::array<float, 4>{left, top, 1.0f, 1.0f}, std::array<float, 4>{right, top, -1.0f, 1.0f},
                                       std::array<float, 4>{left, bottom, 1.0f, -1.0f}, std::array<float, 4>{right, bottom, -1.0f, -1.0f}})
    {
      _batch.Line(x, y, x + dx * arm, y, thickness, color);
      _batch.Line(x, y, x, y + dy * arm, thickness, color);
    }
    char text[96]{};
    std::snprintf(text, sizeof(text), "%s  %.0f M", target.callsign.c_str(), static_cast<double>(target.distanceMetres));
    _batch.Text(FontTiny, centerX, bottom + 4.0f * s, text, color, Neuron::TextAlign::Center);
  }
  if (target.leadOnScreen)
  {
    const float r = 7.0f * s;
    const float x = target.lead.x;
    const float y = target.lead.y;
    _batch.Line(x, y - r, x + r, y, thickness, AMBER);
    _batch.Line(x + r, y, x, y + r, thickness, AMBER);
    _batch.Line(x, y + r, x - r, y, thickness, AMBER);
    _batch.Line(x - r, y, x, y - r, thickness, AMBER);
  }
}

/// The target's figures, top right: its shield and structure, and the module sub-targeted.
[[nodiscard]] Neuron::ScreenRect TargetPanel(HudBatch& _batch, const FlightHudInput& _input)
{
  const float s = _input.scale;
  const TargetReadout& target = _input.target;
  const std::uint32_t color = target.hostile ? RED : ACCENT;
  const float width = std::min(TARGET_PANEL_WIDTH * s, _input.width * 0.4f);
  const float height = TARGET_PANEL_HEIGHT * s;
  const float x = _input.width - width - TARGET_PANEL_MARGIN * s;
  const float y = TARGET_PANEL_MARGIN * s;
  _batch.Rect(x, y, width, height, PANEL_FILL);
  _batch.Outline(x, y, width, height, std::max(1.0f, 1.2f * s), PANEL_EDGE_DIM);
  _batch.Rect(x, y, width, 3.0f * s, color);
  _batch.Text(FontBodyStrong, x + 14.0f * s, y + 10.0f * s, target.callsign, color);
  char text[96]{};
  std::snprintf(text, sizeof(text), "%.0f M", static_cast<double>(target.distanceMetres));
  _batch.Text(FontSmall, x + width - 14.0f * s, y + 12.0f * s, text, TEXT_DIM, Neuron::TextAlign::Right);
  if (target.ship)
  {
    const float barX = x + 14.0f * s;
    const float barWidth = width - 28.0f * s;
    if (target.shieldPercent >= 0)
    {
      Bar(_batch, Rect(barX, y + 52.0f * s, barWidth * 0.48f, BAR_HEIGHT * s), static_cast<float>(target.shieldPercent) / 100.0f, ACCENT,
          target.shieldPercent == 0 ? "SHIELD DOWN" : "SHIELD", s);
    }
    const float hull = static_cast<float>(target.hullPercent) / 100.0f;
    Bar(_batch, Rect(barX + barWidth * 0.52f, y + 52.0f * s, barWidth * 0.48f, BAR_HEIGHT * s), hull,
        target.hullPercent < 50 ? RED : (target.hullPercent < 80 ? AMBER : GREEN), "STRUCTURE", s);
    if (target.module.empty())
    {
      _batch.Text(FontSmall, barX, y + 76.0f * s, "WHOLE SHIP   [ ]: MODULE", TEXT_DIM);
    }
    else
    {
      std::snprintf(text, sizeof(text), "%s  %d %%", target.module.c_str(), target.modulePercent);
      _batch.Text(FontSmall, barX, y + 76.0f * s, text, target.modulePercent < 50 ? RED : TEXT);
    }
  }
  else
  {
    _batch.Text(FontSmall, x + 14.0f * s, y + 52.0f * s, "STATION   G: DOCK HERE", TEXT_DIM);
  }
  return Rect(x, y, width, height);
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
  // Two lines of key help sit under the panel when they are shown.
  const float panelY = _input.height - panelHeight - (_input.showHelp ? 58.0f : 40.0f) * s;
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
  // With a shield, it shares the throttle's row.
  const bool shield = _input.shieldShare >= 0.0f;
  layout.throttleBar = Rect(barsX, panelY + 30.0f * s, shield ? barsWidth * 0.48f : barsWidth, BAR_HEIGHT * s);
  if (shield)
  {
    layout.shieldBar = Rect(barsX + barsWidth * 0.52f, panelY + 30.0f * s, barsWidth * 0.48f, BAR_HEIGHT * s);
    Bar(_batch, layout.shieldBar, _input.shieldShare, _input.shieldUp ? ACCENT : RED, _input.shieldUp ? "SHIELD" : "SHIELD DOWN", s);
  }
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
    const char* flight = "Hold LMB: steer   Space: mouse lock   W/S, wheel: throttle   Tab: thruster   C: cruise   Z: engine   "
                         "A/D/R/F: strafe   Q/E: roll   G: dock   Esc: cancel   H: help   F10: quit";
    const char* combat = "Hold RMB: fire   T, MMB: target   Ctrl+T: clear target   [ ]: module   K: repair";
    const float helpWidth = std::max(_batch.TextWidth(FontSmall, flight), _batch.TextWidth(FontSmall, combat));
    const float lineHeight = _batch.LineHeight(FontSmall);
    const float helpY = _input.height - 26.0f * s - lineHeight;
    layout.help = Rect(centerX - helpWidth * 0.5f, helpY, helpWidth, lineHeight * 2.0f);
    _batch.Text(FontSmall, centerX, helpY, flight, TEXT_DIM, Neuron::TextAlign::Center);
    _batch.Text(FontSmall, centerX, helpY + lineHeight, combat, TEXT_DIM, Neuron::TextAlign::Center);
  }
  if (!_input.statusLine.empty())
  {
    _batch.Text(FontTiny, centerX, 10.0f * s, _input.statusLine, TEXT_DIM, Neuron::TextAlign::Center);
  }
  if (_input.hazard.Active())
  {
    layout.warning = Warning(_batch, _input);
  }
  if (_input.target.active)
  {
    Brackets(_batch, _input);
    layout.targetPanel = TargetPanel(_batch, _input);
  }
  return layout;
}

} // namespace Outpost
