#include "InputScript.h"

#include "FileSys.h"
#include "RecordFile.h"

#include <algorithm>

namespace Outpost
{

namespace
{

constexpr int MAX_TICK = 1'000'000;

[[nodiscard]] std::string LineError(const Neuron::Record& _record, std::string_view _message)
{
  return "line " + std::to_string(_record.line) + ": " + std::string(_message);
}

[[nodiscard]] bool ReadTick(const Neuron::Record& _record, std::string_view _key, int _minimum, int& _out, std::string& _outError)
{
  if (!_record.GetInt(_key, _out) || _out < _minimum || _out > MAX_TICK)
  {
    _outError = LineError(_record, std::string(_key) + " must be a whole number from " + std::to_string(_minimum));
    return false;
  }
  return true;
}

[[nodiscard]] bool ReadPoint(const Neuron::Record& _record, std::string_view _key, float& _outX, float& _outY, std::string& _outError)
{
  const std::string* value = _record.Find(_key);
  float point[2]{};
  if (value == nullptr || !Neuron::ParseFloatList(*value, point, 2))
  {
    _outError = LineError(_record, std::string(_key) + " must be x,y in pixels");
    return false;
  }
  _outX = point[0];
  _outY = point[1];
  return true;
}

[[nodiscard]] bool ReadButton(const Neuron::Record& _record, PointerButton& _out, std::string& _outError)
{
  const std::string* value = _record.Find("button");
  if (value == nullptr || *value == "left")
  {
    _out = PointerButton::Left;
  }
  else if (*value == "right")
  {
    _out = PointerButton::Right;
  }
  else if (*value == "middle")
  {
    _out = PointerButton::Middle;
  }
  else
  {
    _outError = LineError(_record, "button must be left, right or middle");
    return false;
  }
  return true;
}

} // namespace

bool InputScript::Parse(std::string_view _text, InputScript& _outScript, std::string& _outError)
{
  std::vector<Neuron::Record> records;
  if (!Neuron::ParseRecords(_text, records, _outError))
  {
    return false;
  }
  std::vector<InputEvent> events;
  for (const Neuron::Record& record : records)
  {
    int tick = 0;
    if (!ReadTick(record, "tick", 1, tick, _outError))
    {
      return false;
    }
    InputEvent event;
    event.tick = static_cast<std::uint64_t>(tick);
    if (record.keyword == "drag")
    {
      int ticks = 0;
      float fromX = 0.0f;
      float fromY = 0.0f;
      float toX = 0.0f;
      float toY = 0.0f;
      if (!ReadButton(record, event.button, _outError) || !ReadPoint(record, "from", fromX, fromY, _outError) ||
          !ReadPoint(record, "to", toX, toY, _outError) || !ReadTick(record, "ticks", 1, ticks, _outError))
      {
        return false;
      }
      event.kind = InputEvent::Kind::PointerDown;
      event.xPixels = fromX;
      event.yPixels = fromY;
      events.push_back(event);
      for (int step = 1; step <= ticks; ++step)
      {
        const float along = static_cast<float>(step) / static_cast<float>(ticks);
        event.kind = InputEvent::Kind::PointerMove;
        event.tick = static_cast<std::uint64_t>(tick) + static_cast<std::uint64_t>(step);
        event.xPixels = fromX + (toX - fromX) * along;
        event.yPixels = fromY + (toY - fromY) * along;
        events.push_back(event);
      }
      event.kind = InputEvent::Kind::PointerUp;
      events.push_back(event);
    }
    else if (record.keyword == "click")
    {
      if (!ReadButton(record, event.button, _outError) || !ReadPoint(record, "at", event.xPixels, event.yPixels, _outError))
      {
        return false;
      }
      event.kind = InputEvent::Kind::PointerDown;
      events.push_back(event);
      event.kind = InputEvent::Kind::PointerUp;
      events.push_back(event);
    }
    else if (record.keyword == "wheel")
    {
      if (!record.GetFloat("steps", event.wheelSteps))
      {
        _outError = LineError(record, "steps must be a number");
        return false;
      }
      event.kind = InputEvent::Kind::Wheel;
      events.push_back(event);
    }
    else if (record.keyword == "reset" || record.keyword == "help" || record.keyword == "status")
    {
      event.kind = record.keyword == "reset"  ? InputEvent::Kind::ResetView
                   : record.keyword == "help" ? InputEvent::Kind::ToggleHelp
                                              : InputEvent::Kind::ToggleStatus;
      events.push_back(event);
    }
    else
    {
      _outError = LineError(record, "unknown command '" + record.keyword + "'");
      return false;
    }
  }
  std::ranges::stable_sort(events, {}, &InputEvent::tick);
  _outScript.m_events = std::move(events);
  return true;
}

bool InputScript::Load(const std::filesystem::path& _path, InputScript& _outScript, std::string& _outError)
{
  std::string text;
  if (!Neuron::ReadTextFile(_path, text))
  {
    _outError = "cannot read " + _path.string();
    return false;
  }
  if (!Parse(text, _outScript, _outError))
  {
    _outError = _path.filename().string() + ", " + _outError;
    return false;
  }
  return true;
}

std::uint64_t InputScript::LastTick() const noexcept
{
  return m_events.empty() ? 0 : m_events.back().tick;
}

std::span<const InputEvent> InputScript::EventsAt(std::uint64_t _tick) const noexcept
{
  const std::span<const InputEvent> all(m_events);
  const auto first = std::ranges::lower_bound(all, _tick, {}, &InputEvent::tick);
  const auto last = std::ranges::upper_bound(all, _tick, {}, &InputEvent::tick);
  return all.subspan(static_cast<std::size_t>(first - all.begin()), static_cast<std::size_t>(last - first));
}

} // namespace Outpost
