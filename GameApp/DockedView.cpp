#include "DockedView.h"

#include "ButtonSkin.h"
#include "HudPalette.h"
#include "TacticalHud.h"

#include <array>
#include <string_view>

namespace Outpost
{

namespace
{

using Neuron::HudBatch;
using Neuron::ScreenRect;

struct RailEntry
{
  DockedSection section;
  std::string_view label;
  std::string_view icon;
};

constexpr std::array<RailEntry, 4> RAIL{
  RailEntry{DockedSection::Build, "Build", "Icon.Build"}, RailEntry{DockedSection::Yard, "Yard", "Icon.Yard"},
  RailEntry{DockedSection::Fleet, "Fleet", "Icon.Fleet"}, RailEntry{DockedSection::Services, "Services", "Icon.Services"}};

/// A panel: the standard nine-slice frame with the manifest loaded, a plain edged rectangle without.
void Frame(HudBatch& _batch, float _scale, const ScreenRect& _rect)
{
  const float w = _rect.right - _rect.left;
  const float h = _rect.bottom - _rect.top;
  if (const Neuron::Sprite* frame = _batch.FindSprite("Panel.Frame"))
  {
    _batch.NineSlice(*frame, _rect.left, _rect.top, w, h, Neuron::WHITE, _scale * SPRITE_PIXELS_PER_TEXEL);
    return;
  }
  _batch.Rect(_rect.left, _rect.top, w, h, PANEL_FILL);
  _batch.Rect(_rect.left, _rect.top, w, 1.0f, PANEL_EDGE);
  _batch.Rect(_rect.left, _rect.bottom - 1.0f, w, 1.0f, PANEL_EDGE);
}

[[nodiscard]] Neuron::ButtonStyle Style(float _scale)
{
  Neuron::ButtonStyle style;
  style.labelFace = FontBodyStrong;
  style.reasonFace = FontSmall;
  style.scale = _scale;
  style.texelScale = _scale * SPRITE_PIXELS_PER_TEXEL;
  style.iconColor.fill(ACCENT);
  style.labelColor.fill(TEXT);
  style.iconColor[static_cast<std::size_t>(Neuron::ControlState::Disabled)] = TEXT_DIM;
  style.labelColor[static_cast<std::size_t>(Neuron::ControlState::Disabled)] = TEXT_DIM;
  style.labelColor[static_cast<std::size_t>(Neuron::ControlState::Refused)] = AMBER;
  style.reasonColor = AMBER;
  style.focusColor = ACCENT;
  return style;
}

[[nodiscard]] std::string_view SectionTitle(DockedSection _section) noexcept
{
  switch (_section)
  {
  case DockedSection::Build:
    return "CONSTRUCTION";
  case DockedSection::Yard:
    return "YARD";
  case DockedSection::Fleet:
    return "FLEET";
  case DockedSection::Services:
    break;
  }
  return "SERVICES";
}

} // namespace

Neuron::ControlId DockedView::RailControl(DockedSection _section) noexcept
{
  return RAIL_BUILD + static_cast<Neuron::ControlId>(_section);
}

bool DockedView::SectionOf(Neuron::ControlId _id, DockedSection& _outSection) noexcept
{
  if (_id < RAIL_BUILD || _id > RAIL_SERVICES)
  {
    return false;
  }
  _outSection = static_cast<DockedSection>(_id - RAIL_BUILD);
  return true;
}

DockedViewLayout DockedView::Build(HudBatch& _batch, const DockedViewInput& _input) const
{
  DockedViewLayout layout;
  const float s = _input.scale;
  Neuron::ButtonSkin skin;
  std::string skinError;
  if (_batch.Sprites() != nullptr)
  {
    static_cast<void>(skin.Resolve(*_batch.Sprites(), "Button", "Focus.Ring", skinError));
  }
  const Neuron::ButtonStyle style = Style(s);
  const auto stateOf = [&](Neuron::ControlId _id, Neuron::ControlState _authority = Neuron::ControlState::Idle)
  { return _input.focus != nullptr ? _input.focus->StateOf(_id, _authority) : _authority; };
  const auto focused = [&](Neuron::ControlId _id) { return _input.focus != nullptr && _input.focus->IsFocused(_id); };

  // The rail: left, between the objectives and the radar, one button per section.
  const float railButton = 72.0f * s;
  const float railGap = 6.0f * s;
  const float railX = 12.0f * s;
  const float railY = 262.0f * s;
  layout.rail = ScreenRect{railX - 6.0f * s, railY - 6.0f * s, railX + railButton + 6.0f * s,
                           railY + static_cast<float>(RAIL.size()) * (railButton + railGap) + 6.0f * s - railGap};
  Frame(_batch, s, layout.rail);
  for (std::size_t i = 0; i < RAIL.size(); ++i)
  {
    const RailEntry& entry = RAIL[i];
    const Neuron::ControlId id = RailControl(entry.section);
    const float y = railY + static_cast<float>(i) * (railButton + railGap);
    const ScreenRect rect{railX, y, railX + railButton, y + railButton};
    const Neuron::ControlState state =
      stateOf(id, entry.section == _input.section ? Neuron::ControlState::Selected : Neuron::ControlState::Idle);
    Neuron::ButtonContent content;
    content.icon = _batch.FindSprite(entry.icon);
    content.focused = focused(id);
    static_cast<void>(skin.Draw(_batch, ScreenRect{rect.left, rect.top, rect.right, rect.bottom - 18.0f * s}, state, content, style));
    _batch.Text(FontTiny, (rect.left + rect.right) * 0.5f, rect.bottom - 16.0f * s, entry.label,
                state == Neuron::ControlState::Selected ? ACCENT : TEXT_DIM, Neuron::TextAlign::Center);
    layout.controls.push_back(DockedControl{id, rect, true, {}});
  }

  // The inspector: right, below the resources and above the build panel.
  const float inspectorWidth = 380.0f * s;
  const float x0 = _input.width - inspectorWidth - 10.0f * s;
  const float y0 = 70.0f * s;
  const float pad = 20.0f * s;
  const float x = x0 + pad;
  const float innerWidth = inspectorWidth - pad * 2.0f;
  float y = y0 + pad;
  const float bottom = _input.section == DockedSection::Services ? y0 + 420.0f * s : y0 + 190.0f * s;
  layout.inspector = ScreenRect{x0, y0, x0 + inspectorWidth, bottom};
  Frame(_batch, s, layout.inspector);
  _batch.Text(FontSmall, x, y, SectionTitle(_input.section), ACCENT);
  y += 26.0f * s;
  _batch.Text(FontHeading, x, y, _input.stationLabel, TEXT);
  y += 34.0f * s;
  _batch.Text(FontSmall, x, y, "Docked: " + _input.shipLabel + (_input.berth.empty() ? std::string() : ", " + _input.berth), TEXT_DIM);
  y += 34.0f * s;
  _batch.Rect(x, y, innerWidth, 1.0f, PANEL_EDGE_DIM);
  y += 16.0f * s;

  if (_input.section != DockedSection::Services)
  {
    const std::string_view line = _input.section == DockedSection::Build  ? "No construction plans are held yet."
                                  : _input.section == DockedSection::Yard ? "No hull plans are held yet."
                                                                          : "No other ships answer to you yet.";
    _batch.Text(FontBody, x, y, line, TEXT_DIM);
    return layout;
  }

  // Launch: offered when the ship can fly, pending until the server answers, refused with its reason.
  const float buttonHeight = 64.0f * s;
  const ScreenRect launch{x, y, x + innerWidth, y + buttonHeight};
  const bool launchEnabled = _input.launchAvailable && !_input.launchPending;
  const std::string launchReason = !_input.launchAvailable ? _input.launchUnavailable : _input.launchRefused;
  Neuron::ControlState launchState = Neuron::ControlState::Idle;
  if (_input.launchPending)
  {
    launchState = Neuron::ControlState::Pending;
  }
  else if (!_input.launchRefused.empty())
  {
    launchState = Neuron::ControlState::Refused;
  }
  Neuron::ButtonContent launchContent;
  launchContent.icon = _batch.FindSprite("Icon.Launch");
  launchContent.label = _input.launchPending ? "Launching..." : "Launch";
  launchContent.reason = launchReason;
  launchContent.focused = focused(LAUNCH);
  const Neuron::ControlState launchDrawn = _input.launchAvailable ? stateOf(LAUNCH, launchState) : Neuron::ControlState::Disabled;
  static_cast<void>(skin.Draw(_batch, launch, launchDrawn, launchContent, style));
  layout.controls.push_back(
    DockedControl{LAUNCH, launch, launchEnabled, _input.launchAvailable ? std::string() : _input.launchUnavailable});
  y += buttonHeight + 34.0f * s;

  // Repair: shown, and not yet offered, so its place and its reason are visible now.
  const ScreenRect repair{x, y, x + innerWidth, y + buttonHeight};
  constexpr std::string_view REPAIR_REASON = "The yard does not repair hulls yet";
  Neuron::ButtonContent repairContent;
  repairContent.icon = _batch.FindSprite("Icon.Repair");
  repairContent.label = "Repair";
  repairContent.reason = REPAIR_REASON;
  repairContent.focused = focused(REPAIR);
  static_cast<void>(skin.Draw(_batch, repair, Neuron::ControlState::Disabled, repairContent, style));
  layout.controls.push_back(DockedControl{REPAIR, repair, false, std::string(REPAIR_REASON)});
  y += buttonHeight + 40.0f * s;

  _batch.Rect(x, y, innerWidth, 1.0f, PANEL_EDGE_DIM);
  y += 14.0f * s;
  _batch.Text(FontSmall, x, y, "Saved on launch, on docking and on quit.", TEXT_DIM);
  return layout;
}

} // namespace Outpost
