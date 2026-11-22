#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

enum class PointerButton : std::uint8_t
{
  Left,
  Right,
  Middle
};

/// One input as the client takes it, at the server tick of the frame it belongs to.
struct InputEvent
{
  enum class Kind : std::uint8_t
  {
    PointerDown,
    PointerMove,
    PointerUp,
    Wheel,
    ResetView,
    ToggleHelp,
    ToggleStatus
  };

  std::uint64_t tick = 0;
  Kind kind = Kind::PointerMove;
  PointerButton button = PointerButton::Left;
  float xPixels = 0.0f;
  float yPixels = 0.0f;
  float wheelSteps = 0.0f;

  friend bool operator==(const InputEvent&, const InputEvent&) = default;
};

/// A timeline of input that drives the smoke test (plan task F5), in the record format of
/// Scene.txt (ADR-002). Each command names the server tick it starts at, from 1:
///
///   drag   tick=10 button=left from=960,540 to=1160,540 ticks=30   # press, move each tick, release
///   click  tick=60 at=1500,700 button=left                         # press and release in one tick
///   wheel  tick=70 steps=-2
///   reset  tick=80                                                 # the view the scene authored
///   help   tick=90                                                 # toggles the help line
///   status tick=95                                                 # toggles the status line
///
/// Positions are pixels of the window the script was written for. The commands expand into
/// events, kept in tick order with the file's order inside a tick.
class InputScript
{
public:
  [[nodiscard]] static bool Parse(std::string_view _text, InputScript& _outScript, std::string& _outError);
  [[nodiscard]] static bool Load(const std::filesystem::path& _path, InputScript& _outScript, std::string& _outError);

  [[nodiscard]] const std::vector<InputEvent>& Events() const noexcept
  {
    return m_events;
  }
  /// The tick of the last event, or 0 for an empty script.
  [[nodiscard]] std::uint64_t LastTick() const noexcept;
  /// The events at exactly this tick, in order.
  [[nodiscard]] std::span<const InputEvent> EventsAt(std::uint64_t _tick) const noexcept;

private:
  std::vector<InputEvent> m_events;
};

} // namespace Outpost
