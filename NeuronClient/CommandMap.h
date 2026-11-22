#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

/// A key, a mouse button or a wheel direction: anything a binding can name. Keyboard keys are Win32
/// virtual-key codes, as WM_KEYDOWN reports them; the mouse takes codes above the keyboard's.
using InputKey = std::uint16_t;
inline constexpr InputKey NO_KEY = 0;
inline constexpr InputKey KEY_MOUSE_LEFT = 0x101;
inline constexpr InputKey KEY_MOUSE_RIGHT = 0x102;
inline constexpr InputKey KEY_MOUSE_MIDDLE = 0x103;
inline constexpr InputKey KEY_WHEEL_UP = 0x104;
inline constexpr InputKey KEY_WHEEL_DOWN = 0x105;

/// The key a bindings file names ("A", "7", "F11", "Tab", "Space", "Escape", "Up", "MouseLeft",
/// "WheelDown"), or NO_KEY.
[[nodiscard]] InputKey KeyFromName(std::string_view _name) noexcept;
/// The name KeyFromName reads, for key hints; empty for a key without one.
[[nodiscard]] std::string_view KeyName(InputKey _key) noexcept;

using CommandId = std::uint16_t;
inline constexpr CommandId NO_COMMAND = 0xFFFF;
using ContextId = std::uint16_t;
inline constexpr ContextId NO_CONTEXT = 0xFFFF;

enum class CommandPhase : std::uint8_t
{
  Pressed,
  Released, // only for a hold command
  Repeated  // only for a command that takes auto-repeat
};

struct CommandEvent
{
  CommandId command = NO_COMMAND;
  CommandPhase phase = CommandPhase::Pressed;

  friend bool operator==(const CommandEvent&, const CommandEvent&) = default;
};

/// The command layer of the Frontier blueprint (ch. 09 §3.1; plan task 1.5). Devices produce key
/// events; bindings, which are data, turn them into commands; contexts scope the bindings, so that
/// one key can mean one thing in flight and another in the tactical view. The engine knows none of
/// the game's commands by name: they come from the bindings file, and the game asks for them.
///
///   context name=Flying priority=10      # the higher priority wins a key bound in two live contexts
///   command name=Thruster hold=true      # hold: Pressed on the key's down and Released on its up
///   command name=ThrottleUp hold=true repeat=false
///   bind command=Thruster context=Flying key=Tab
///
/// A key's down is resolved once, against the contexts live at that moment, and its up releases
/// whatever that down pressed. So a key held across a context switch releases the command it
/// pressed, and never one of the new context's: a thruster cannot stick on.
class CommandMap
{
public:
  [[nodiscard]] static bool Parse(std::string_view _text, std::string_view _fileName, CommandMap& _outMap, std::string& _outError);
  [[nodiscard]] static bool Load(const std::filesystem::path& _path, CommandMap& _outMap, std::string& _outError);

  [[nodiscard]] CommandId FindCommand(std::string_view _name) const noexcept;
  [[nodiscard]] ContextId FindContext(std::string_view _name) const noexcept;
  [[nodiscard]] const std::string& CommandName(CommandId _command) const noexcept;
  [[nodiscard]] std::size_t CommandCount() const noexcept
  {
    return m_commands.size();
  }

  void SetContextActive(ContextId _context, bool _active) noexcept;
  [[nodiscard]] bool IsContextActive(ContextId _context) const noexcept;

  /// A key went down; _repeat for the keyboard's auto-repeat of a key already down.
  void KeyDown(InputKey _key, bool _repeat);
  void KeyUp(InputKey _key);
  /// A wheel notch: a press and release in one, of KEY_WHEEL_UP or KEY_WHEEL_DOWN.
  void Wheel(InputKey _direction);
  /// Releases every key that is down, as when the window loses the focus.
  void ReleaseAll();

  /// The commands since the last call, in the order their events arrived.
  [[nodiscard]] std::vector<CommandEvent> TakeEvents();
  /// Whether a hold command is held down now.
  [[nodiscard]] bool IsHeld(CommandId _command) const noexcept;
  /// The first key bound to a command, for key hints, or NO_KEY.
  [[nodiscard]] InputKey KeyFor(CommandId _command) const noexcept;

private:
  struct Command
  {
    std::string name;
    bool hold = false;
    bool repeat = false;
  };

  struct Context
  {
    std::string name;
    int priority = 0;
    bool active = false;
  };

  struct Binding
  {
    InputKey key = NO_KEY;
    CommandId command = NO_COMMAND;
    ContextId context = NO_CONTEXT;
  };

  struct DownKey
  {
    InputKey key = NO_KEY;
    CommandId command = NO_COMMAND; // what its down pressed, if anything
  };

  [[nodiscard]] CommandId Resolve(InputKey _key) const noexcept;

  std::vector<Command> m_commands;
  std::vector<Context> m_contexts;
  std::vector<Binding> m_bindings;
  std::vector<DownKey> m_down;
  std::vector<CommandEvent> m_events;
};

} // namespace Neuron
