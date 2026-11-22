#include "ButtonSkin.h"

#include <algorithm>
#include <cmath>

namespace Neuron
{

bool ButtonSkin::Resolve(const SpriteSheet& _sheet, std::string_view _prefix, std::string_view _focusRing, std::string& _outError)
{
  std::array<const Sprite*, CONTROL_STATE_COUNT> frames{};
  for (std::size_t i = 0; i < CONTROL_STATE_COUNT; ++i)
  {
    const std::string name = std::string(_prefix) + "." + std::string(ControlStateName(static_cast<ControlState>(i)));
    frames[i] = _sheet.Find(name);
    if (frames[i] == nullptr)
    {
      _outError = "the sprite manifest has no " + name;
      return false;
    }
  }
  const Sprite* focusRing = nullptr;
  if (!_focusRing.empty())
  {
    focusRing = _sheet.Find(_focusRing);
    if (focusRing == nullptr)
    {
      _outError = "the sprite manifest has no " + std::string(_focusRing);
      return false;
    }
  }
  m_frames = frames;
  m_focusRing = focusRing;
  return true;
}

bool ButtonSkin::ShowsReason(ControlState _state, bool _focused, std::string_view _reason) noexcept
{
  if (_reason.empty())
  {
    return false;
  }
  return _state == ControlState::Disabled || _state == ControlState::Refused || _state == ControlState::Hover ||
         _state == ControlState::Pressed || _focused;
}

ButtonLayout ButtonSkin::Draw(HudBatch& _batch, const ScreenRect& _rect, ControlState _state, const ButtonContent& _content,
                              const ButtonStyle& _style) const
{
  const auto index = static_cast<std::size_t>(_state);
  const float x = _rect.left;
  const float y = _rect.top;
  const float width = _rect.right - _rect.left;
  const float height = _rect.bottom - _rect.top;
  ButtonLayout layout;
  layout.frame = _rect;
  if (width <= 0.0f || height <= 0.0f)
  {
    return layout;
  }

  if (_content.focused && m_focusRing != nullptr)
  {
    const float grow = std::round(3.0f * _style.scale);
    _batch.NineSlice(*m_focusRing, x - grow, y - grow, width + 2.0f * grow, height + 2.0f * grow, _style.focusColor, _style.texelScale);
  }
  if (const Sprite* frame = m_frames[index]; frame != nullptr)
  {
    _batch.NineSlice(*frame, x, y, width, height, _style.frameColor[index], _style.texelScale);
  }
  else
  {
    _batch.Rect(x, y, width, height, _style.frameColor[index]);
  }

  const float pad = std::round(6.0f * _style.scale);
  const bool hasLabel = !_content.label.empty();
  const bool showReason = ShowsReason(_state, _content.focused, _content.reason);
  const float labelHeight = hasLabel ? _batch.LineHeight(_style.labelFace) : 0.0f;
  const float reasonHeight = showReason ? _batch.LineHeight(_style.reasonFace) : 0.0f;
  layout.reasonShown = showReason;
  layout.reasonInside = showReason && hasLabel && labelHeight + reasonHeight + 2.0f * pad <= height;

  float textLeft = x + pad;
  if (_content.icon != nullptr)
  {
    // Square, inside the padding; centred without a label, at the left beside one.
    const float side = std::max(0.0f, std::min(height, hasLabel ? height : width) - 2.0f * pad);
    const float iconX = hasLabel ? x + pad : std::round(x + (width - side) * 0.5f);
    const float iconY = std::round(y + (height - side) * 0.5f);
    layout.icon = ScreenRect{iconX, iconY, iconX + side, iconY + side};
    _batch.Image(*_content.icon, iconX, iconY, side, side, _style.iconColor[index]);
    textLeft = iconX + side + pad;
  }

  if (hasLabel)
  {
    const float block = layout.reasonInside ? labelHeight + reasonHeight : labelHeight;
    layout.labelX = std::round(textLeft);
    layout.labelY = std::round(y + (height - block) * 0.5f);
    _batch.Text(_style.labelFace, layout.labelX, layout.labelY, _content.label, _style.labelColor[index]);
  }
  if (showReason)
  {
    if (layout.reasonInside)
    {
      layout.reasonX = layout.labelX;
      layout.reasonY = layout.labelY + labelHeight;
    }
    else
    {
      layout.reasonX = std::round(x);
      layout.reasonY = std::round(y + height + 2.0f * _style.scale);
    }
    _batch.Text(_style.reasonFace, layout.reasonX, layout.reasonY, _content.reason, _style.reasonColor);
  }
  return layout;
}

} // namespace Neuron
