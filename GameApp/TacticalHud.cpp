#include "TacticalHud.h"

#include "ButtonSkin.h"
#include "HudPalette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace Outpost
{

using DirectX::XMFLOAT2;
using Neuron::HudBatch;
using Neuron::Rgba;

namespace
{

[[nodiscard]] std::uint32_t HealthColor(int _percent) noexcept
{
  return _percent < 50 ? RED : (_percent < 80 ? AMBER : GREEN);
}

[[nodiscard]] std::string UpperCase(std::string _text)
{
  for (char& character : _text)
  {
    if (character >= 'a' && character <= 'z')
    {
      character = static_cast<char>(character - 'a' + 'A');
    }
  }
  return _text;
}

[[nodiscard]] std::string FormatAmount(std::int64_t _amount)
{
  char text[32]{};
  if (_amount >= 1000)
  {
    std::snprintf(text, sizeof(text), "%.1f K", static_cast<double>(_amount) / 1000.0);
  }
  else
  {
    std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(_amount));
  }
  return text;
}

[[nodiscard]] bool HasPanelSprites(const HudBatch& _batch) noexcept
{
  return _batch.FindSprite("Panel.Fill") != nullptr && _batch.FindSprite("Panel.Edge") != nullptr;
}

/// A panel over the rectangle. With the sprite manifest loaded it is a nine-slice (ADR-023): the
/// standard panel, PANEL_FILL edged with PANEL_EDGE, is Panel.Frame as authored, and any other
/// pairing is Panel.Fill and Panel.Edge tinted with its colours. Without the manifest it is the
/// polygon with its corners cut at 45 degrees; _cuts is top-left, top-right, bottom-right,
/// bottom-left. Returns whether the panel was drawn from sprites.
bool Panel(HudBatch& _batch, float _scale, float _x, float _y, float _w, float _h, std::array<float, 4> _cuts, std::uint32_t _fill,
           std::uint32_t _edge, float _edgeWidth)
{
  const float texelScale = _scale * SPRITE_PIXELS_PER_TEXEL;
  const Neuron::Sprite* frame = _batch.FindSprite("Panel.Frame");
  if (frame != nullptr && _fill == PANEL_FILL && _edge == PANEL_EDGE && _edgeWidth > 0.0f)
  {
    _batch.NineSlice(*frame, _x, _y, _w, _h, Neuron::WHITE, texelScale);
    return true;
  }
  const Neuron::Sprite* fill = _batch.FindSprite("Panel.Fill");
  const Neuron::Sprite* edge = _batch.FindSprite("Panel.Edge");
  if (fill != nullptr && edge != nullptr)
  {
    _batch.NineSlice(*fill, _x, _y, _w, _h, _fill, texelScale);
    if (_edgeWidth > 0.0f)
    {
      _batch.NineSlice(*edge, _x, _y, _w, _h, _edge, texelScale);
    }
    return true;
  }
  const std::array<XMFLOAT2, 8> points = {XMFLOAT2{_x + _cuts[0], _y},           XMFLOAT2{_x + _w - _cuts[1], _y},
                                          XMFLOAT2{_x + _w, _y + _cuts[1]},      XMFLOAT2{_x + _w, _y + _h - _cuts[2]},
                                          XMFLOAT2{_x + _w - _cuts[2], _y + _h}, XMFLOAT2{_x + _cuts[3], _y + _h},
                                          XMFLOAT2{_x, _y + _h - _cuts[3]},      XMFLOAT2{_x, _y + _cuts[0]}};
  _batch.Polygon(points, _fill);
  if (_edgeWidth > 0.0f)
  {
    _batch.PolyLine(points, true, _edgeWidth, _edge);
  }
  return false;
}

void Bar(HudBatch& _batch, float _x, float _y, float _w, float _h, float _fraction, std::uint32_t _color)
{
  _batch.Rect(_x, _y, _w, _h, BAR_TRACK);
  _batch.Rect(_x, _y, std::round(_w * std::clamp(_fraction, 0.0f, 1.0f)), _h, _color);
}

void Hexagon(HudBatch& _batch, float _cx, float _cy, float _r, float _width, std::uint32_t _color, bool _fill, std::uint32_t _fillColor)
{
  std::array<XMFLOAT2, 6> points{};
  for (int i = 0; i < 6; ++i)
  {
    const float angle = (static_cast<float>(i) * 60.0f - 90.0f) * std::numbers::pi_v<float> / 180.0f;
    points[static_cast<std::size_t>(i)] = XMFLOAT2{_cx + std::cos(angle) * _r, _cy + std::sin(angle) * _r};
  }
  if (_fill)
  {
    _batch.Polygon(points, _fillColor);
  }
  _batch.PolyLine(points, true, _width, _color);
}

/// An isometric cube outline, the station emblem used for the logo and the build button.
void CubeGlyph(HudBatch& _batch, float _cx, float _cy, float _r, float _width, std::uint32_t _color)
{
  const float h = _r * 0.5f;
  const float w = _r * 0.866f;
  const std::array<XMFLOAT2, 6> outer = {XMFLOAT2{_cx, _cy - _r}, XMFLOAT2{_cx + w, _cy - h}, XMFLOAT2{_cx + w, _cy + h},
                                         XMFLOAT2{_cx, _cy + _r}, XMFLOAT2{_cx - w, _cy + h}, XMFLOAT2{_cx - w, _cy - h}};
  _batch.PolyLine(outer, true, _width, _color);
  _batch.Line(_cx - w, _cy - h, _cx, _cy, _width, _color);
  _batch.Line(_cx + w, _cy - h, _cx, _cy, _width, _color);
  _batch.Line(_cx, _cy, _cx, _cy + _r, _width, _color);
}

void ResourceIcon(HudBatch& _batch, std::string_view _name, float _cx, float _cy, float _u)
{
  if (_name == "Alloy")
  {
    const float r = _u * 0.48f;
    const float h = r * 0.5f;
    const float w = r * 0.866f;
    const std::array<XMFLOAT2, 4> top = {XMFLOAT2{_cx, _cy - r}, XMFLOAT2{_cx + w, _cy - h}, XMFLOAT2{_cx, _cy},
                                         XMFLOAT2{_cx - w, _cy - h}};
    const std::array<XMFLOAT2, 4> left = {XMFLOAT2{_cx - w, _cy - h}, XMFLOAT2{_cx, _cy}, XMFLOAT2{_cx, _cy + r},
                                          XMFLOAT2{_cx - w, _cy + h}};
    const std::array<XMFLOAT2, 4> right = {XMFLOAT2{_cx, _cy}, XMFLOAT2{_cx + w, _cy - h}, XMFLOAT2{_cx + w, _cy + h},
                                           XMFLOAT2{_cx, _cy + r}};
    _batch.Polygon(top, Rgba(214, 232, 244));
    _batch.Polygon(left, Rgba(150, 178, 198));
    _batch.Polygon(right, Rgba(104, 132, 152));
  }
  else if (_name == "Ore")
  {
    for (int i = 0; i < 3; ++i)
    {
      const float y = _cy + _u * (0.22f - static_cast<float>(i) * 0.22f);
      const float inset = static_cast<float>(i) * _u * 0.08f;
      const std::array<XMFLOAT2, 4> bar = {
        XMFLOAT2{_cx - _u * 0.42f + inset, y + _u * 0.09f}, XMFLOAT2{_cx - _u * 0.30f + inset, y - _u * 0.09f},
        XMFLOAT2{_cx + _u * 0.30f - inset, y - _u * 0.09f}, XMFLOAT2{_cx + _u * 0.42f - inset, y + _u * 0.09f}};
      _batch.Polygon(bar, i == 2 ? Rgba(200, 210, 218) : Rgba(146, 158, 170));
    }
  }
  else if (_name == "Water")
  {
    std::array<XMFLOAT2, 14> drop{};
    drop[0] = XMFLOAT2{_cx, _cy - _u * 0.48f};
    for (int i = 0; i < 13; ++i)
    {
      const float angle = (-30.0f + static_cast<float>(i) * 20.0f) * std::numbers::pi_v<float> / 180.0f;
      drop[static_cast<std::size_t>(i) + 1] = XMFLOAT2{_cx + std::cos(angle) * _u * 0.28f, _cy + _u * 0.16f + std::sin(angle) * _u * 0.28f};
    }
    _batch.Polygon(drop, Rgba(64, 178, 255));
  }
  else if (_name == "Power")
  {
    const std::array<XMFLOAT2, 3> upper = {XMFLOAT2{_cx + _u * 0.12f, _cy - _u * 0.5f}, XMFLOAT2{_cx - _u * 0.26f, _cy + _u * 0.06f},
                                           XMFLOAT2{_cx + _u * 0.04f, _cy + _u * 0.06f}};
    const std::array<XMFLOAT2, 3> lower = {XMFLOAT2{_cx - _u * 0.04f, _cy - _u * 0.06f}, XMFLOAT2{_cx + _u * 0.26f, _cy - _u * 0.06f},
                                           XMFLOAT2{_cx - _u * 0.12f, _cy + _u * 0.5f}};
    _batch.Polygon(upper, Rgba(255, 200, 40));
    _batch.Polygon(lower, Rgba(255, 200, 40));
  }
  else
  {
    for (const float dx : {-0.2f, 0.2f})
    {
      _batch.Disc(_cx + dx * _u, _cy - _u * 0.2f, _u * 0.14f, Rgba(90, 210, 240), 16);
      const std::array<XMFLOAT2, 4> body = {
        XMFLOAT2{_cx + (dx - 0.22f) * _u, _cy + _u * 0.38f}, XMFLOAT2{_cx + (dx - 0.16f) * _u, _cy + _u * 0.04f},
        XMFLOAT2{_cx + (dx + 0.16f) * _u, _cy + _u * 0.04f}, XMFLOAT2{_cx + (dx + 0.22f) * _u, _cy + _u * 0.38f}};
      _batch.Polygon(body, Rgba(90, 210, 240));
    }
  }
}

void ActionIcon(HudBatch& _batch, int _index, float _cx, float _cy, float _u, std::uint32_t _color)
{
  const float w = std::max(1.5f, _u * 0.08f);
  switch (_index)
  {
  case 0: // move: four arrows
    for (int i = 0; i < 4; ++i)
    {
      const float angle = static_cast<float>(i) * std::numbers::pi_v<float> * 0.5f;
      const float dx = std::cos(angle);
      const float dy = std::sin(angle);
      _batch.Line(_cx + dx * _u * 0.12f, _cy + dy * _u * 0.12f, _cx + dx * _u * 0.42f, _cy + dy * _u * 0.42f, w, _color);
      const XMFLOAT2 tip{_cx + dx * _u * 0.48f, _cy + dy * _u * 0.48f};
      _batch.Triangle(tip, XMFLOAT2{tip.x - dx * _u * 0.16f - dy * _u * 0.11f, tip.y - dy * _u * 0.16f + dx * _u * 0.11f},
                      XMFLOAT2{tip.x - dx * _u * 0.16f + dy * _u * 0.11f, tip.y - dy * _u * 0.16f - dx * _u * 0.11f}, _color);
    }
    break;
  case 1: // formation: three linked nodes
  {
    const std::array<XMFLOAT2, 3> nodes = {XMFLOAT2{_cx, _cy - _u * 0.32f}, XMFLOAT2{_cx - _u * 0.34f, _cy + _u * 0.28f},
                                           XMFLOAT2{_cx + _u * 0.34f, _cy + _u * 0.28f}};
    _batch.Line(nodes[0].x, nodes[0].y, nodes[1].x, nodes[1].y, w, _color);
    _batch.Line(nodes[0].x, nodes[0].y, nodes[2].x, nodes[2].y, w, _color);
    for (const XMFLOAT2& node : nodes)
    {
      _batch.Disc(node.x, node.y, _u * 0.11f, _color, 14);
    }
    break;
  }
  case 2: // shield
  {
    const std::array<XMFLOAT2, 6> shield = {
      XMFLOAT2{_cx, _cy - _u * 0.44f}, XMFLOAT2{_cx + _u * 0.36f, _cy - _u * 0.3f},  XMFLOAT2{_cx + _u * 0.32f, _cy + _u * 0.08f},
      XMFLOAT2{_cx, _cy + _u * 0.46f}, XMFLOAT2{_cx - _u * 0.32f, _cy + _u * 0.08f}, XMFLOAT2{_cx - _u * 0.36f, _cy - _u * 0.3f}};
    _batch.Polygon(shield, Neuron::FadeColor(_color, 0.85f));
    break;
  }
  case 3: // repair: wrench
    _batch.Line(_cx - _u * 0.32f, _cy + _u * 0.32f, _cx + _u * 0.12f, _cy - _u * 0.12f, w * 1.8f, _color);
    _batch.Circle(_cx + _u * 0.2f, _cy - _u * 0.2f, _u * 0.17f, w * 1.4f, _color, 20);
    break;
  default: // build: station cube
    CubeGlyph(_batch, _cx, _cy, _u * 0.42f, w, _color);
    break;
  }
}

} // namespace

std::vector<Neuron::FontSpec> TacticalHud::Fonts(float _scale)
{
  auto size = [_scale](float _pixels) { return std::max(8, static_cast<int>(std::lround(_pixels * _scale))); };
  const std::wstring face = L"Bahnschrift";
  return {
    Neuron::FontSpec{face, size(15.0f), 400}, Neuron::FontSpec{face, size(17.0f), 400}, Neuron::FontSpec{face, size(17.0f), 600},
    Neuron::FontSpec{face, size(20.0f), 700}, Neuron::FontSpec{face, size(25.0f), 700}, Neuron::FontSpec{face, size(12.0f), 400},
  };
}

HudLayout TacticalHud::Build(HudBatch& _batch, const HudInput& _input) const
{
  HudLayout layout;
  _batch.SetLayer(HudBatch::BACK);
  DrawMarkers(_batch, _input);
  DrawTitle(_batch, _input);
  DrawObjectives(_batch, _input);
  DrawResources(_batch, _input);
  DrawRadar(_batch, _input);
  DrawShipPanel(_batch, _input, layout);
  DrawBuildPanel(_batch, _input, layout);
  _batch.SetLayer(HudBatch::FRONT);
  if (_input.showHelp)
  {
    _batch.Text(FontSmall, _input.width * 0.5f, 10.0f * _input.scale,
                "LMB drag: orbit    RMB / MMB drag: pan    Wheel: zoom    Click: select ship    R: reset view    H: hide help", TEXT_DIM,
                Neuron::TextAlign::Center);
  }
  if (!_input.statusLine.empty())
  {
    _batch.Text(FontTiny, _input.width * 0.5f, 34.0f * _input.scale, _input.statusLine, TEXT_DIM, Neuron::TextAlign::Center);
  }
  return layout;
}

void TacticalHud::DrawMarkers(HudBatch& _batch, const HudInput& _input) const
{
  const float s = _input.scale;
  const float centerX = _input.width * 0.5f;
  const float centerY = _input.height * 0.5f;
  for (const ShipMarker& marker : _input.markers)
  {
    const Neuron::ProjectedBounds& bounds = marker.bounds;
    if (bounds.visibility == Neuron::ScreenVisibility::OnScreen)
    {
      const Neuron::ScreenRect& r = bounds.rect;
      const float width = r.right - r.left;
      const float height = r.bottom - r.top;
      // Health bar centred over the ship, kept on screen.
      const float barWidth = std::clamp(width * 0.32f, 34.0f * s, 76.0f * s);
      const float barX = std::clamp((r.left + r.right) * 0.5f - barWidth * 0.5f, 4.0f, _input.width - barWidth - 4.0f);
      const float barY = std::clamp(r.top - 14.0f * s, 4.0f, _input.height - 10.0f);
      _batch.Rect(barX - 1.0f, barY - 1.0f, barWidth + 2.0f, std::round(5.0f * s) + 2.0f, Rgba(0, 0, 0, 140));
      Bar(_batch, barX, barY, barWidth, std::round(5.0f * s), static_cast<float>(marker.hullPercent) / 100.0f,
          HealthColor(marker.hullPercent));
      if (marker.selected)
      {
        const float pad = 10.0f * s;
        const float x0 = r.left - pad;
        const float y0 = r.top - pad + 4.0f * s;
        const float x1 = r.right + pad;
        const float y1 = r.bottom + pad;
        const float arm = std::min({24.0f * s, width * 0.3f + pad, height * 0.3f + pad});
        const float t = std::max(2.0f, 2.2f * s);
        const std::uint32_t color = Neuron::FadeColor(ACCENT, 0.85f + 0.15f * std::sin(_input.timeSeconds * 4.0f));
        for (const auto& [cx, cy, dx, dy] : {std::array<float, 4>{x0, y0, 1.0f, 1.0f}, std::array<float, 4>{x1, y0, -1.0f, 1.0f},
                                             std::array<float, 4>{x1, y1, -1.0f, -1.0f}, std::array<float, 4>{x0, y1, 1.0f, -1.0f}})
        {
          _batch.Rect(dx > 0.0f ? cx : cx - arm, dy > 0.0f ? cy : cy - t, arm, t, color);
          _batch.Rect(dx > 0.0f ? cx : cx - t, dy > 0.0f ? cy : cy - arm, t, arm, color);
        }
        _batch.SetLayer(HudBatch::FRONT);
        _batch.Text(FontTiny, x1 + 6.0f * s, y0, marker.callsign, ACCENT);
        _batch.SetLayer(HudBatch::BACK);
      }
      continue;
    }
    // Off screen or behind the camera: an arrow at the screen edge pointing toward the ship.
    const XMFLOAT2 direction = bounds.edgeDirection;
    const float insetX = centerX - 70.0f * s;
    const float insetY = centerY - 70.0f * s;
    const float t = std::min(std::abs(direction.x) > 1e-4f ? insetX / std::abs(direction.x) : 1e9f,
                             std::abs(direction.y) > 1e-4f ? insetY / std::abs(direction.y) : 1e9f);
    const float ax = centerX + direction.x * t;
    const float ay = centerY + direction.y * t;
    const float size = (marker.selected ? 14.0f : 10.0f) * s;
    const std::uint32_t color = marker.selected ? ACCENT : HealthColor(marker.hullPercent);
    _batch.Triangle(
      XMFLOAT2{ax + direction.x * size, ay + direction.y * size},
      XMFLOAT2{ax - direction.x * size * 0.6f - direction.y * size * 0.7f, ay - direction.y * size * 0.6f + direction.x * size * 0.7f},
      XMFLOAT2{ax - direction.x * size * 0.6f + direction.y * size * 0.7f, ay - direction.y * size * 0.6f - direction.x * size * 0.7f},
      color);
    std::string label = marker.callsign;
    if (bounds.visibility == Neuron::ScreenVisibility::BehindCamera)
    {
      label += " (behind)";
    }
    _batch.Text(FontTiny, ax - direction.x * 22.0f * s, ay - direction.y * 22.0f * s - 6.0f * s, label, color, Neuron::TextAlign::Center);
  }
}

void TacticalHud::DrawTitle(HudBatch& _batch, const HudInput& _input) const
{
  const float s = _input.scale;
  if (HasPanelSprites(_batch))
  {
    Panel(_batch, s, 0.0f, 0.0f, 372.0f * s, 70.0f * s, {}, PANEL_FILL_DEEP, PANEL_EDGE_DIM, 1.5f);
  }
  else
  {
    const std::array<XMFLOAT2, 5> shape = {XMFLOAT2{0.0f, 0.0f}, XMFLOAT2{372.0f * s, 0.0f}, XMFLOAT2{372.0f * s, 46.0f * s},
                                           XMFLOAT2{348.0f * s, 70.0f * s}, XMFLOAT2{0.0f, 70.0f * s}};
    _batch.Polygon(shape, PANEL_FILL_DEEP);
    _batch.Line(0.0f, 70.0f * s, 348.0f * s, 70.0f * s, 1.5f, PANEL_EDGE_DIM);
    _batch.Line(348.0f * s, 70.0f * s, 372.0f * s, 46.0f * s, 1.5f, PANEL_EDGE_DIM);
  }
  Hexagon(_batch, 38.0f * s, 35.0f * s, 23.0f * s, 2.2f * s, ACCENT, true, Rgba(12, 40, 52, 230));
  CubeGlyph(_batch, 38.0f * s, 35.0f * s, 11.0f * s, 2.0f * s, ACCENT);
  _batch.Text(FontTitle, 74.0f * s, 9.0f * s, "OUTPOST: FRONTIER", TEXT);
  _batch.Text(FontSmall, 76.0f * s, 41.0f * s, "SHIPYARD COMMAND  /  KESSLER REACH", ACCENT);
}

void TacticalHud::DrawObjectives(HudBatch& _batch, const HudInput& _input) const
{
  if (_input.snapshot == nullptr)
  {
    return;
  }
  const float s = _input.scale;
  const auto& objectives = _input.snapshot->objectives;
  const float x = 12.0f * s;
  const float y = 94.0f * s;
  const float rowHeight = 30.0f * s;
  const float h = 48.0f * s + rowHeight * static_cast<float>(objectives.size()) + 6.0f * s;
  if (Panel(_batch, s, x, y, 262.0f * s, h, {0.0f, 14.0f * s, 0.0f, 14.0f * s}, PANEL_FILL, PANEL_EDGE_DIM, 1.2f))
  {
    // Inside the rounded corners, so the accent does not stand proud of the frame.
    _batch.Rect(x + 2.0f * s, y + 8.0f * s, 3.0f * s, h - 16.0f * s, ACCENT);
  }
  else
  {
    _batch.Rect(x, y, 3.0f * s, h, ACCENT);
  }
  _batch.Text(FontHeading, x + 14.0f * s, y + 9.0f * s, "OBJECTIVES", ACCENT);
  for (std::size_t i = 0; i < objectives.size(); ++i)
  {
    const float rowY = y + 46.0f * s + static_cast<float>(i) * rowHeight;
    const float box = 17.0f * s;
    const float boxX = x + 14.0f * s;
    if (objectives[i].complete)
    {
      _batch.Rect(boxX, rowY, box, box, Rgba(28, 120, 60, 230));
      _batch.Outline(boxX, rowY, box, box, std::max(1.0f, 1.5f * s), GREEN);
      _batch.Line(boxX + box * 0.2f, rowY + box * 0.52f, boxX + box * 0.42f, rowY + box * 0.76f, 2.4f * s, GREEN);
      _batch.Line(boxX + box * 0.42f, rowY + box * 0.76f, boxX + box * 0.84f, rowY + box * 0.22f, 2.4f * s, GREEN);
    }
    else
    {
      _batch.Outline(boxX, rowY, box, box, std::max(1.0f, 1.5f * s), TEXT_DIM);
    }
    _batch.Text(FontBody, boxX + box + 11.0f * s, rowY - 1.0f * s, objectives[i].text, objectives[i].complete ? TEXT_DIM : TEXT);
  }
}

void TacticalHud::DrawResources(HudBatch& _batch, const HudInput& _input) const
{
  if (_input.snapshot == nullptr || _input.snapshot->resources.empty())
  {
    return;
  }
  const float s = _input.scale;
  const float icon = 22.0f * s;
  const float gap = 26.0f * s;
  std::vector<std::string> labels;
  float total = 0.0f;
  for (const ResourceState& resource : _input.snapshot->resources)
  {
    std::string label = FormatAmount(resource.amount);
    if (resource.capacity > 0)
    {
      label = std::to_string(resource.amount) + " / " + std::to_string(resource.capacity);
    }
    total += icon + 8.0f * s + _batch.TextWidth(FontBodyStrong, label) + gap;
    labels.push_back(std::move(label));
  }
  const float h = 50.0f * s;
  const float x0 = _input.width - total - 28.0f * s;
  if (HasPanelSprites(_batch))
  {
    Panel(_batch, s, x0, 0.0f, total + 28.0f * s, h, {}, PANEL_FILL_DEEP, PANEL_EDGE_DIM, 1.5f);
  }
  else
  {
    Panel(_batch, s, x0, 0.0f, total + 28.0f * s, h, {0.0f, 0.0f, 0.0f, 20.0f * s}, PANEL_FILL_DEEP, PANEL_EDGE_DIM, 0.0f);
    _batch.Line(x0 + 20.0f * s, h, _input.width, h, 1.5f, PANEL_EDGE_DIM);
    _batch.Line(x0, h - 20.0f * s, x0 + 20.0f * s, h, 1.5f, PANEL_EDGE_DIM);
  }
  float x = x0 + 26.0f * s;
  for (std::size_t i = 0; i < labels.size(); ++i)
  {
    const ResourceState& resource = _input.snapshot->resources[i];
    if (resource.capacity > 0)
    {
      _batch.Rect(x - gap * 0.5f, 10.0f * s, 1.0f, h - 20.0f * s, PANEL_EDGE_DIM);
    }
    ResourceIcon(_batch, resource.name, x + icon * 0.5f, h * 0.5f, icon);
    x += icon + 8.0f * s;
    x += _batch.Text(FontBodyStrong, x, h * 0.5f - _batch.LineHeight(FontBodyStrong) * 0.5f, labels[i], TEXT) + gap;
  }
}

void TacticalHud::DrawRadar(HudBatch& _batch, const HudInput& _input) const
{
  const float s = _input.scale;
  const float size = 224.0f * s;
  const float x = 10.0f * s;
  const float y = _input.height - size - 10.0f * s;
  Panel(_batch, s, x, y, size, size, {18.0f * s, 18.0f * s, 18.0f * s, 18.0f * s}, PANEL_FILL, PANEL_EDGE, 1.5f);
  const float cx = x + size * 0.5f;
  const float cy = y + size * 0.5f;
  const float radius = size * 0.4f;
  _batch.Disc(cx, cy, radius, Rgba(6, 30, 40, 200), 48);
  _batch.Circle(cx, cy, radius, 1.4f, PANEL_EDGE_DIM, 64);
  _batch.Circle(cx, cy, radius * 0.5f, 1.0f, Rgba(30, 110, 128, 90), 48);
  _batch.Line(cx - radius, cy, cx + radius, cy, 1.0f, Rgba(30, 110, 128, 110));
  _batch.Line(cx, cy - radius, cx, cy + radius, 1.0f, Rgba(30, 110, 128, 110));

  // The camera's field of view as a wedge, pointing up.
  const float halfFov = _input.cameraFovDegrees * 0.5f * std::numbers::pi_v<float> / 180.0f * 1.4f;
  const std::array<XMFLOAT2, 3> wedge = {XMFLOAT2{cx, cy}, XMFLOAT2{cx - std::sin(halfFov) * radius, cy - std::cos(halfFov) * radius},
                                         XMFLOAT2{cx + std::sin(halfFov) * radius, cy - std::cos(halfFov) * radius}};
  _batch.Polygon(wedge, Rgba(64, 226, 236, 26));

  // A slow sweep with a fading trail.
  const float sweep = std::fmod(_input.timeSeconds * 0.9f, 2.0f * std::numbers::pi_v<float>);
  for (int i = 0; i < 12; ++i)
  {
    const float a0 = sweep - static_cast<float>(i) * 0.06f;
    const float a1 = a0 - 0.06f;
    _batch.Triangle(XMFLOAT2{cx, cy}, XMFLOAT2{cx + std::sin(a0) * radius, cy - std::cos(a0) * radius},
                    XMFLOAT2{cx + std::sin(a1) * radius, cy - std::cos(a1) * radius},
                    Rgba(64, 226, 236, static_cast<std::uint8_t>(60 - i * 5)));
  }

  const float yaw = _input.cameraYawDegrees * std::numbers::pi_v<float> / 180.0f;
  const float cosYaw = std::cos(yaw);
  const float sinYaw = std::sin(yaw);
  const float pixelsPerUnit = radius / _input.radarRangeWorld;
  for (const RadarBlip& blip : _input.blips)
  {
    const float right = blip.x * cosYaw - blip.z * sinYaw;
    const float forward = blip.x * sinYaw + blip.z * cosYaw;
    const float bx = cx + right * pixelsPerUnit;
    const float by = cy - forward * pixelsPerUnit;
    const float dx = bx - cx;
    const float dy = by - cy;
    if (dx * dx + dy * dy > radius * radius * 0.96f)
    {
      continue;
    }
    const float blipSize = std::clamp(blip.sizeWorld * pixelsPerUnit, 3.0f * s, 14.0f * s);
    switch (blip.kind)
    {
    case RadarBlip::Kind::Ship:
      _batch.Rect(bx - 3.0f * s, by - 3.0f * s, 6.0f * s, 6.0f * s, GREEN);
      break;
    case RadarBlip::Kind::SelectedShip:
      _batch.Rect(bx - 4.0f * s, by - 4.0f * s, 8.0f * s, 8.0f * s, ACCENT);
      _batch.Outline(bx - 7.0f * s, by - 7.0f * s, 14.0f * s, 14.0f * s, 1.0f, ACCENT);
      break;
    case RadarBlip::Kind::Station:
      _batch.Rect(bx - blipSize * 0.5f, by - blipSize * 0.5f, blipSize, blipSize, Rgba(170, 196, 206, 200));
      break;
    case RadarBlip::Kind::Rock:
      _batch.Disc(bx, by, blipSize * 0.5f, Rgba(120, 104, 96, 170), 14);
      break;
    case RadarBlip::Kind::Blueprint:
      _batch.Outline(bx - blipSize * 0.5f, by - blipSize * 0.5f, blipSize, blipSize, 1.0f, ACCENT);
      break;
    }
  }
  _batch.Triangle(XMFLOAT2{cx, cy - 7.0f * s}, XMFLOAT2{cx - 5.0f * s, cy + 5.0f * s}, XMFLOAT2{cx + 5.0f * s, cy + 5.0f * s}, ACCENT);
  _batch.Text(FontTiny, x + 16.0f * s, y + 10.0f * s, "TACTICAL", TEXT_DIM);
  char range[32]{};
  std::snprintf(range, sizeof(range), "%.0f m", static_cast<double>(_input.radarRangeWorld));
  _batch.Text(FontTiny, x + size - 16.0f * s, y + size - 24.0f * s, range, TEXT_DIM, Neuron::TextAlign::Right);
}

void TacticalHud::DrawShipPanel(HudBatch& _batch, const HudInput& _input, HudLayout& _layout) const
{
  const float s = _input.scale;
  const float x0 = 250.0f * s;
  const float x1 = _input.width - 440.0f * s;
  const float h = 136.0f * s;
  const float y0 = _input.height - h - 12.0f * s;
  const float w = x1 - x0;
  if (w < 280.0f * s)
  {
    return;
  }
  Panel(_batch, s, x0, y0, w, h, {14.0f * s, 14.0f * s, 14.0f * s, 14.0f * s}, PANEL_FILL, PANEL_EDGE, 1.5f);
  const ShipState* ship = _input.selected;
  if (ship == nullptr)
  {
    _batch.Text(FontBody, x0 + w * 0.5f, y0 + h * 0.5f - 10.0f * s, "NO SHIP SELECTED  -  click a ship to inspect it", TEXT_DIM,
                Neuron::TextAlign::Center);
    return;
  }
  // Left: the ship's outline, drawn by the client into this rectangle.
  _layout.silhouette = Neuron::ScreenRect{x0 + 14.0f * s, y0 + 12.0f * s, x0 + 264.0f * s, y0 + h - 12.0f * s};

  // Identity and hull.
  float x = x0 + 282.0f * s;
  for (int i = 0; i < 2; ++i)
  {
    const float cy = y0 + 26.0f * s + static_cast<float>(i) * 7.0f * s;
    _batch.Line(x, cy, x + 9.0f * s, cy + 7.0f * s, 2.2f * s, ACCENT);
    _batch.Line(x + 9.0f * s, cy + 7.0f * s, x + 18.0f * s, cy, 2.2f * s, ACCENT);
  }
  _batch.Text(FontHeading, x + 30.0f * s, y0 + 20.0f * s, UpperCase(ship->shipClass) + "   " + ship->callsign, TEXT);
  char hull[32]{};
  std::snprintf(hull, sizeof(hull), "Hull  %d%%", ship->hullPercent);
  _batch.Text(FontBody, x, y0 + 56.0f * s, hull, TEXT);
  Bar(_batch, x, y0 + 86.0f * s, 196.0f * s, 13.0f * s, static_cast<float>(ship->hullPercent) / 100.0f, Rgba(56, 224, 190));
  _batch.Text(FontTiny, x, y0 + 106.0f * s, std::to_string(ship->modules.size()) + " modules fitted", TEXT_DIM);

  const float dividerA = x0 + 500.0f * s;
  if (w < 520.0f * s)
  {
    return;
  }
  _batch.Rect(dividerA, y0 + 16.0f * s, 1.0f, h - 32.0f * s, PANEL_EDGE_DIM);

  // Damaged modules, worst first, straight from the snapshot.
  const std::vector<ShipModuleState> damaged = ship->DamagedModules();
  const float mx = dividerA + 20.0f * s;
  if (damaged.empty())
  {
    _batch.Text(FontBodyStrong, mx, y0 + 18.0f * s, "ALL MODULES NOMINAL", GREEN);
  }
  else
  {
    _batch.Text(FontBodyStrong, mx, y0 + 16.0f * s, "DAMAGED MODULES (" + std::to_string(damaged.size()) + ")", AMBER);
    const std::size_t rows = std::min<std::size_t>(damaged.size(), 3);
    for (std::size_t i = 0; i < rows; ++i)
    {
      const ShipModuleState& module = damaged[i];
      const float rowY = y0 + 48.0f * s + static_cast<float>(i) * 27.0f * s;
      const std::uint32_t color = HealthColor(module.healthPercent);
      _batch.Disc(mx + 8.0f * s, rowY + 10.0f * s, 7.0f * s, Neuron::FadeColor(color, 0.35f), 16);
      _batch.Circle(mx + 8.0f * s, rowY + 10.0f * s, 7.0f * s, 1.6f * s, color, 16);
      _batch.Text(FontBody, mx + 24.0f * s, rowY, module.name, TEXT);
      Bar(_batch, mx + 190.0f * s, rowY + 7.0f * s, 96.0f * s, 7.0f * s, static_cast<float>(module.healthPercent) / 100.0f, color);
      _batch.Text(FontBody, mx + 340.0f * s, rowY, std::to_string(module.healthPercent) + "%", TEXT, Neuron::TextAlign::Right);
    }
  }

  // Actions.
  const float dividerB = mx + 362.0f * s;
  const float button = 50.0f * s;
  const float spacing = 60.0f * s;
  if (dividerB + 20.0f * s + spacing * 5.0f > x1)
  {
    return;
  }
  _batch.Rect(dividerB, y0 + 16.0f * s, 1.0f, h - 32.0f * s, PANEL_EDGE_DIM);
  // With the sprite manifest the actions are buttons of the shared control skin (ADR-023), and
  // repair and build take their icons from the mask atlas. Nothing here is interactive yet.
  Neuron::ButtonSkin skin;
  std::string skinError;
  const bool skinned = _batch.Sprites() != nullptr && skin.Resolve(*_batch.Sprites(), "Button", {}, skinError);
  Neuron::ButtonStyle style;
  style.scale = s;
  style.texelScale = s * SPRITE_PIXELS_PER_TEXEL;
  style.iconColor.fill(ACCENT);
  for (int i = 0; i < 5; ++i)
  {
    const float bx = dividerB + 22.0f * s + static_cast<float>(i) * spacing;
    const float by = y0 + (h - button) * 0.5f;
    if (!skinned)
    {
      Panel(_batch, s, bx, by, button, button, {6.0f * s, 6.0f * s, 6.0f * s, 6.0f * s}, Rgba(10, 34, 44, 230), PANEL_EDGE_DIM, 1.2f);
      ActionIcon(_batch, i, bx + button * 0.5f, by + button * 0.5f, button * 0.62f, ACCENT);
      continue;
    }
    Neuron::ButtonContent content;
    content.icon = i == 3 ? _batch.FindSprite("Icon.Repair") : (i == 4 ? _batch.FindSprite("Icon.Build") : nullptr);
    static_cast<void>(skin.Draw(_batch, Neuron::ScreenRect{bx, by, bx + button, by + button}, Neuron::ControlState::Idle, content, style));
    if (content.icon == nullptr)
    {
      ActionIcon(_batch, i, bx + button * 0.5f, by + button * 0.5f, button * 0.62f, ACCENT);
    }
  }
}

void TacticalHud::DrawBuildPanel(HudBatch& _batch, const HudInput& _input, HudLayout& _layout) const
{
  const float s = _input.scale;
  const float w = 420.0f * s;
  const float h = 190.0f * s;
  const float x = _input.width - w - 10.0f * s;
  const float y = _input.height - h - 10.0f * s;
  Panel(_batch, s, x, y, w, h, {14.0f * s, 0.0f, 14.0f * s, 0.0f}, PANEL_FILL, PANEL_EDGE, 1.5f);

  const std::array<const char*, 3> tabs = {"BUILD", "DESIGN", "FLEET"};
  const float tabWidth = w / 3.0f;
  for (std::size_t i = 0; i < tabs.size(); ++i)
  {
    const float tx = x + tabWidth * static_cast<float>(i);
    if (i == 0)
    {
      _batch.RectGradient(tx + 6.0f * s, y + 6.0f * s, tabWidth - 12.0f * s, 34.0f * s, Rgba(30, 110, 128, 150), Rgba(16, 60, 72, 120));
      _batch.Rect(tx + 6.0f * s, y + 38.0f * s, tabWidth - 12.0f * s, 3.0f * s, ACCENT);
    }
    _batch.Text(FontBodyStrong, tx + tabWidth * 0.5f, y + 13.0f * s, tabs[i], i == 0 ? ACCENT : TEXT_DIM, Neuron::TextAlign::Center);
  }
  _batch.Rect(x + 6.0f * s, y + 44.0f * s, w - 12.0f * s, 1.0f, PANEL_EDGE_DIM);

  const float slot = 88.0f * s;
  const float slotGap = (w - 28.0f * s - slot * 4.0f) / 3.0f;
  for (std::size_t i = 0; i < 4; ++i)
  {
    const float sx = x + 14.0f * s + static_cast<float>(i) * (slot + slotGap);
    const float sy = y + 56.0f * s;
    if (const Neuron::Sprite* inset = _batch.FindSprite("Panel.Inset"); inset != nullptr)
    {
      _batch.NineSlice(*inset, sx, sy, slot, slot, Neuron::WHITE, s * SPRITE_PIXELS_PER_TEXEL);
      _batch.RectGradient(sx + 3.0f * s, sy + slot * 0.55f, slot - 6.0f * s, slot * 0.45f - 5.0f * s, Rgba(64, 226, 236, 0), ACCENT_SOFT);
    }
    else
    {
      Panel(_batch, s, sx, sy, slot, slot, {8.0f * s, 0.0f, 8.0f * s, 0.0f}, Rgba(12, 30, 40, 235), PANEL_EDGE_DIM, 1.2f);
      _batch.RectGradient(sx + 1.0f, sy + slot * 0.55f, slot - 2.0f, slot * 0.45f - 1.0f, Rgba(64, 226, 236, 0), ACCENT_SOFT);
    }
    if (i < _input.buildLabels.size())
    {
      _layout.buildSlots.push_back(Neuron::ScreenRect{sx + 4.0f * s, sy + 4.0f * s, sx + slot - 4.0f * s, sy + slot - 4.0f * s});
      _batch.SetLayer(HudBatch::FRONT);
      _batch.Text(FontTiny, sx + slot * 0.5f, sy + slot + 8.0f * s, _input.buildLabels[i], TEXT_DIM, Neuron::TextAlign::Center);
      _batch.SetLayer(HudBatch::BACK);
    }
  }
}

} // namespace Outpost
