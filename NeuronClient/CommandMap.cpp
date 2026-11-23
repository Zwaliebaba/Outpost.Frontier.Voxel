#include "CommandMap.h"

#include "FileSys.h"
#include "RecordFile.h"

#include <algorithm>
#include <array>
#include <utility>

namespace Neuron
{

namespace
{

struct NamedKey
{
  std::string_view name;
  InputKey key;
};

// Win32 virtual-key codes, written out so that this header and its tests need no <windows.h>.
constexpr std::array<NamedKey, 32> NAMED_KEYS{{{"Backspace", 0x08},
                                               {"Tab", 0x09},
                                               {"Enter", 0x0D},
                                               {"Shift", 0x10},
                                               {"Control", 0x11},
                                               {"Alt", 0x12},
                                               {"Pause", 0x13},
                                               {"Escape", 0x1B},
                                               {"Space", 0x20},
                                               {"PageUp", 0x21},
                                               {"PageDown", 0x22},
                                               {"End", 0x23},
                                               {"Home", 0x24},
                                               {"Left", 0x25},
                                               {"Up", 0x26},
                                               {"Right", 0x27},
                                               {"Down", 0x28},
                                               {"Insert", 0x2D},
                                               {"Delete", 0x2E},
                                               {"Semicolon", 0xBA},
                                               {"Equals", 0xBB},
                                               {"Comma", 0xBC},
                                               {"Minus", 0xBD},
                                               {"Period", 0xBE},
                                               {"Backquote", 0xC0},
                                               {"BracketLeft", 0xDB},
                                               {"BracketRight", 0xDD},
                                               {"MouseLeft", KEY_MOUSE_LEFT},
                                               {"MouseRight", KEY_MOUSE_RIGHT},
                                               {"MouseMiddle", KEY_MOUSE_MIDDLE},
                                               {"WheelUp", KEY_WHEEL_UP},
                                               {"WheelDown", KEY_WHEEL_DOWN}}};

constexpr InputKey VK_F1_CODE = 0x70;
constexpr int FUNCTION_KEYS = 24;

[[nodiscard]] std::string LineError(std::string_view _fileName, const Record& _record, std::string_view _message)
{
  return std::string(_fileName) + " line " + std::to_string(_record.line) + ": " + std::string(_message);
}

[[nodiscard]] bool ReadFlag(const Record& _record, std::string_view _key, bool& _out)
{
  return _record.Find(_key) == nullptr || _record.GetBool(_key, _out);
}

[[nodiscard]] bool IsModifier(InputKey _key) noexcept
{
  return _key == KeyFromName("Shift") || _key == KeyFromName("Control") || _key == KeyFromName("Alt");
}

} // namespace

InputKey KeyFromName(std::string_view _name) noexcept
{
  if (_name.size() == 1)
  {
    const char c = _name[0];
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
    {
      return static_cast<InputKey>(c); // the virtual-key code of a letter or digit is its character
    }
    return NO_KEY;
  }
  if (_name.size() >= 2 && _name[0] == 'F')
  {
    int number = 0;
    if (ParseInt(_name.substr(1), number) && number >= 1 && number <= FUNCTION_KEYS)
    {
      return static_cast<InputKey>(VK_F1_CODE + number - 1);
    }
  }
  for (const NamedKey& named : NAMED_KEYS)
  {
    if (named.name == _name)
    {
      return named.key;
    }
  }
  return NO_KEY;
}

std::string_view KeyName(InputKey _key) noexcept
{
  static constexpr std::array<std::string_view, 36> SINGLE{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B",
                                                           "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
                                                           "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
  static constexpr std::array<std::string_view, FUNCTION_KEYS> FUNCTION{"F1",  "F2",  "F3",  "F4",  "F5",  "F6",  "F7",  "F8",
                                                                        "F9",  "F10", "F11", "F12", "F13", "F14", "F15", "F16",
                                                                        "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24"};
  if (_key >= '0' && _key <= '9')
  {
    return SINGLE[static_cast<std::size_t>(_key - '0')];
  }
  if (_key >= 'A' && _key <= 'Z')
  {
    return SINGLE[static_cast<std::size_t>(10 + _key - 'A')];
  }
  if (_key >= VK_F1_CODE && _key < VK_F1_CODE + FUNCTION_KEYS)
  {
    return FUNCTION[static_cast<std::size_t>(_key - VK_F1_CODE)];
  }
  for (const NamedKey& named : NAMED_KEYS)
  {
    if (named.key == _key)
    {
      return named.name;
    }
  }
  return {};
}

bool CommandMap::Parse(std::string_view _text, std::string_view _fileName, CommandMap& _outMap, std::string& _outError)
{
  std::vector<Record> records;
  if (!ParseRecords(_text, records, _outError))
  {
    _outError = std::string(_fileName) + ", " + _outError;
    return false;
  }
  CommandMap map;
  // Contexts and commands first, so that a binding may come before what it names.
  for (const Record& record : records)
  {
    if (record.keyword == "context")
    {
      Context context;
      if (!record.GetString("name", context.name) || !record.GetInt("priority", context.priority))
      {
        _outError = LineError(_fileName, record, "a context needs name= and priority=");
        return false;
      }
      if (map.FindContext(context.name) != NO_CONTEXT)
      {
        _outError = LineError(_fileName, record, "context '" + context.name + "' is defined twice");
        return false;
      }
      map.m_contexts.push_back(std::move(context));
    }
    else if (record.keyword == "command")
    {
      Command command;
      if (!record.GetString("name", command.name) || !ReadFlag(record, "hold", command.hold) || !ReadFlag(record, "repeat", command.repeat))
      {
        _outError = LineError(_fileName, record, "a command needs name=, and hold= and repeat= are true or false");
        return false;
      }
      if (map.FindCommand(command.name) != NO_COMMAND)
      {
        _outError = LineError(_fileName, record, "command '" + command.name + "' is defined twice");
        return false;
      }
      map.m_commands.push_back(std::move(command));
    }
    else if (record.keyword != "bind")
    {
      _outError = LineError(_fileName, record, "unknown keyword '" + record.keyword + "'");
      return false;
    }
  }
  for (const Record& record : records)
  {
    if (record.keyword != "bind")
    {
      continue;
    }
    std::string commandName;
    std::string contextName;
    std::string keyName;
    if (!record.GetString("command", commandName) || !record.GetString("context", contextName) || !record.GetString("key", keyName))
    {
      _outError = LineError(_fileName, record, "a binding needs command=, context= and key=");
      return false;
    }
    // A chord names its modifier first: key=Control+T.
    const std::size_t plus = keyName.find('+');
    const InputKey modifier = plus == std::string::npos ? NO_KEY : KeyFromName(std::string_view(keyName).substr(0, plus));
    const InputKey key = KeyFromName(plus == std::string::npos ? std::string_view(keyName) : std::string_view(keyName).substr(plus + 1));
    if (plus != std::string::npos && !IsModifier(modifier))
    {
      _outError = LineError(_fileName, record, "a chord in '" + keyName + "' begins with Shift, Control or Alt");
      return false;
    }
    Binding binding{key, map.FindCommand(commandName), map.FindContext(contextName), modifier};
    if (binding.key == NO_KEY)
    {
      _outError = LineError(_fileName, record, "unknown key '" + keyName + "'");
      return false;
    }
    if (binding.command == NO_COMMAND || binding.context == NO_CONTEXT)
    {
      _outError =
        LineError(_fileName, record,
                  binding.command == NO_COMMAND ? "unknown command '" + commandName + "'" : "unknown context '" + contextName + "'");
      return false;
    }
    for (const Binding& other : map.m_bindings)
    {
      if (other.key == binding.key && other.modifier == binding.modifier && other.context == binding.context)
      {
        std::string message = "key '";
        message += keyName;
        message += "' is bound twice in context '";
        message += contextName;
        message += "'";
        _outError = LineError(_fileName, record, message);
        return false;
      }
    }
    map.m_bindings.push_back(binding);
  }
  _outMap = std::move(map);
  return true;
}

bool CommandMap::Load(const std::filesystem::path& _path, CommandMap& _outMap, std::string& _outError)
{
  std::string text;
  if (!ReadTextFile(_path, text))
  {
    _outError = "cannot read " + _path.string();
    return false;
  }
  return Parse(text, _path.filename().string(), _outMap, _outError);
}

CommandId CommandMap::FindCommand(std::string_view _name) const noexcept
{
  for (std::size_t i = 0; i < m_commands.size(); ++i)
  {
    if (m_commands[i].name == _name)
    {
      return static_cast<CommandId>(i);
    }
  }
  return NO_COMMAND;
}

ContextId CommandMap::FindContext(std::string_view _name) const noexcept
{
  for (std::size_t i = 0; i < m_contexts.size(); ++i)
  {
    if (m_contexts[i].name == _name)
    {
      return static_cast<ContextId>(i);
    }
  }
  return NO_CONTEXT;
}

const std::string& CommandMap::CommandName(CommandId _command) const noexcept
{
  static const std::string NONE;
  return _command < m_commands.size() ? m_commands[_command].name : NONE;
}

void CommandMap::SetContextActive(ContextId _context, bool _active) noexcept
{
  if (_context < m_contexts.size())
  {
    m_contexts[_context].active = _active;
  }
}

bool CommandMap::IsContextActive(ContextId _context) const noexcept
{
  return _context < m_contexts.size() && m_contexts[_context].active;
}

CommandId CommandMap::Resolve(InputKey _key) const noexcept
{
  // A chord whose modifier is held beats the key alone; then the binding of the live context with
  // the highest priority; between equals, the earlier context.
  const auto held = [this](InputKey _modifier) { return std::ranges::find(m_down, _modifier, &DownKey::key) != m_down.end(); };
  const Binding* best = nullptr;
  for (const Binding& binding : m_bindings)
  {
    const Context& context = m_contexts[binding.context];
    if (binding.key != _key || !context.active || (binding.modifier != NO_KEY && !held(binding.modifier)))
    {
      continue;
    }
    const bool chord = binding.modifier != NO_KEY;
    const bool bestChord = best != nullptr && best->modifier != NO_KEY;
    if (best == nullptr || (chord && !bestChord) || (chord == bestChord && context.priority > m_contexts[best->context].priority))
    {
      best = &binding;
    }
  }
  return best != nullptr ? best->command : NO_COMMAND;
}

void CommandMap::KeyDown(InputKey _key, bool _repeat)
{
  const auto down = std::ranges::find(m_down, _key, &DownKey::key);
  if (down != m_down.end())
  {
    // A repeat, or a down whose up was lost: the key keeps the command its first down pressed.
    if (_repeat && down->command != NO_COMMAND && m_commands[down->command].repeat)
    {
      m_events.push_back(CommandEvent{down->command, CommandPhase::Repeated});
    }
    return;
  }
  const CommandId command = Resolve(_key);
  m_down.push_back(DownKey{_key, command});
  if (command != NO_COMMAND)
  {
    m_events.push_back(CommandEvent{command, CommandPhase::Pressed});
  }
}

void CommandMap::KeyUp(InputKey _key)
{
  const auto down = std::ranges::find(m_down, _key, &DownKey::key);
  if (down == m_down.end())
  {
    return;
  }
  const CommandId command = down->command;
  m_down.erase(down);
  if (command != NO_COMMAND && m_commands[command].hold)
  {
    m_events.push_back(CommandEvent{command, CommandPhase::Released});
  }
}

void CommandMap::Wheel(InputKey _direction)
{
  KeyDown(_direction, false);
  KeyUp(_direction);
}

void CommandMap::ReleaseAll()
{
  while (!m_down.empty())
  {
    KeyUp(m_down.back().key);
  }
}

std::vector<CommandEvent> CommandMap::TakeEvents()
{
  return std::exchange(m_events, {});
}

bool CommandMap::IsHeld(CommandId _command) const noexcept
{
  return _command != NO_COMMAND && std::ranges::find(m_down, _command, &DownKey::command) != m_down.end();
}

InputKey CommandMap::KeyFor(CommandId _command) const noexcept
{
  const auto found = std::ranges::find(m_bindings, _command, &Binding::command);
  return found != m_bindings.end() ? found->key : NO_KEY;
}

} // namespace Neuron
