#pragma once

#include "ControlState.h"
#include "HudBatch.h"
#include "ScreenProjection.h"
#include "SpriteSheet.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Neuron
{

/// Fonts and colours a button is drawn with. Every colour is indexed by ControlState.
struct ButtonStyle
{
  std::size_t labelFace = 0;
  std::size_t reasonFace = 0;
  float scale = 1.0f;      // HUD scale: padding and gaps, 1 at the 1080-line reference
  float texelScale = 1.0f; // pixels per texel for the nine-slice corners
  std::array<std::uint32_t, CONTROL_STATE_COUNT> frameColor{WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE};
  std::array<std::uint32_t, CONTROL_STATE_COUNT> iconColor{WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE};
  std::array<std::uint32_t, CONTROL_STATE_COUNT> labelColor{WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE};
  std::uint32_t reasonColor = WHITE;
  std::uint32_t focusColor = WHITE;
};

/// What one button shows. The reason is the line that explains a disabled or refused control.
struct ButtonContent
{
  const Sprite* icon = nullptr;
  std::string_view label;
  std::string_view reason;
  bool focused = false;
};

/// Where Draw put the parts of a button, for tests and for anything drawn beside it.
struct ButtonLayout
{
  ScreenRect frame{};
  ScreenRect icon{}; // empty when there is no icon
  float labelX = 0.0f;
  float labelY = 0.0f;
  bool reasonShown = false;
  bool reasonInside = false; // the reason sits inside the frame, or else just below it
  float reasonX = 0.0f;
  float reasonY = 0.0f;
};

/// A button drawn from one nine-slice frame per control state, with an optional icon, a label
/// and a reason line (ADR-023). The frames are the sprites "<prefix>.Idle" to "<prefix>.Refused";
/// an optional focus ring is a nine-slice mask drawn around the frame while the button has the
/// keyboard focus. The skin knows nothing of what a button does.
class ButtonSkin
{
public:
  /// Looks up every state's frame and the focus ring. Every state's frame must exist; the focus
  /// ring may be empty to draw none.
  [[nodiscard]] bool Resolve(const SpriteSheet& _sheet, std::string_view _prefix, std::string_view _focusRing, std::string& _outError);

  [[nodiscard]] bool IsResolved() const noexcept
  {
    return m_frames[0] != nullptr;
  }
  [[nodiscard]] const Sprite* Frame(ControlState _state) const noexcept
  {
    return m_frames[static_cast<std::size_t>(_state)];
  }

  /// A disabled or refused control always shows its reason; any other shows one only while it is
  /// hovered, pressed or focused.
  [[nodiscard]] static bool ShowsReason(ControlState _state, bool _focused, std::string_view _reason) noexcept;

  /// Draws the button over _rect. With a label the icon sits at the left and the label beside it;
  /// without one the icon is centred. The reason goes under the label when the frame is tall
  /// enough for both lines, and just below the frame when it is not. Without resolved frames the
  /// frame is a plain rectangle in the state's frame colour.
  ButtonLayout Draw(HudBatch& _batch, const ScreenRect& _rect, ControlState _state, const ButtonContent& _content,
                    const ButtonStyle& _style) const;

private:
  std::array<const Sprite*, CONTROL_STATE_COUNT> m_frames{};
  const Sprite* m_focusRing = nullptr;
};

} // namespace Neuron
