#include "SaveRecord.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace Outpost
{

namespace
{

template <typename T> [[nodiscard]] std::string Shortest(T _value)
{
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), _value);
  return std::string(buffer.data(), result.ptr);
}

/// Splits "a,b,c" into exactly _count parts read by _read.
template <typename T, typename Fn> [[nodiscard]] bool ReadList(std::string_view _text, T* _out, int _count, Fn _read)
{
  for (int i = 0; i < _count; ++i)
  {
    const std::size_t comma = _text.find(',');
    const bool last = i + 1 == _count;
    if (last != (comma == std::string_view::npos) || !_read(last ? _text : _text.substr(0, comma), _out[i]))
    {
      return false;
    }
    if (!last)
    {
      _text.remove_prefix(comma + 1);
    }
  }
  return true;
}

} // namespace

std::string SaveText(float _value)
{
  return Shortest(_value);
}

std::string SaveText(double _value)
{
  return Shortest(_value);
}

std::string SaveText(std::int64_t _value)
{
  return Shortest(_value);
}

std::string SaveText(std::uint64_t _value)
{
  return Shortest(_value);
}

std::string SaveText(int _value)
{
  return Shortest(_value);
}

std::string SaveText(std::span<const Neuron::CellSpan> _value)
{
  std::string text;
  for (const Neuron::CellSpan& span : _value)
  {
    text += (text.empty() ? "" : ",") + std::to_string(span.start) + ":" + std::to_string(span.count);
  }
  return text;
}

std::string SaveText(std::span<const std::int32_t> _value)
{
  std::string text;
  for (const std::int32_t value : _value)
  {
    text += (text.empty() ? "" : ",") + std::to_string(value);
  }
  return text;
}

std::string SaveText(bool _value)
{
  return _value ? "1" : "0";
}

std::string SaveText(const UniversePosition& _value)
{
  return Shortest(_value.x) + "," + Shortest(_value.y) + "," + Shortest(_value.z);
}

std::string SaveText(const DirectX::XMFLOAT3& _value)
{
  return Shortest(_value.x) + "," + Shortest(_value.y) + "," + Shortest(_value.z);
}

std::string SaveText(const DirectX::XMFLOAT4& _value)
{
  return Shortest(_value.x) + "," + Shortest(_value.y) + "," + Shortest(_value.z) + "," + Shortest(_value.w);
}

bool SaveRecord::Fail(std::string_view _message)
{
  if (m_ok)
  {
    m_error = std::string(m_section) + " line " + std::to_string(m_record.line) + ": " + std::string(_message);
  }
  m_ok = false;
  return false;
}

bool SaveRecord::AllowOnly(std::initializer_list<std::string_view> _keys)
{
  for (const Neuron::RecordField& field : m_record.fields)
  {
    if (std::ranges::find(_keys, std::string_view(field.key)) == _keys.end())
    {
      return Fail("unknown key '" + field.key + "' on '" + m_record.keyword + "'");
    }
  }
  return m_ok;
}

const std::string* SaveRecord::Value(std::string_view _key)
{
  if (!m_ok)
  {
    return nullptr;
  }
  const std::string* value = m_record.Find(_key);
  if (value == nullptr)
  {
    static_cast<void>(Fail("'" + m_record.keyword + "' is missing '" + std::string(_key) + "'"));
  }
  return value;
}

void SaveRecord::Malformed(std::string_view _key, std::string_view _expected)
{
  static_cast<void>(Fail("'" + std::string(_key) + "' must be " + std::string(_expected)));
}

void SaveRecord::Read(std::string_view _key, std::string& _out)
{
  if (const std::string* value = Value(_key))
  {
    _out = *value;
  }
}

void SaveRecord::Read(std::string_view _key, float& _out)
{
  if (const std::string* value = Value(_key); value != nullptr && !Neuron::ParseFloat(*value, _out))
  {
    Malformed(_key, "a number");
  }
}

void SaveRecord::Read(std::string_view _key, double& _out)
{
  if (const std::string* value = Value(_key); value != nullptr && !Neuron::ParseDouble(*value, _out))
  {
    Malformed(_key, "a number");
  }
}

void SaveRecord::Read(std::string_view _key, std::int64_t& _out)
{
  if (const std::string* value = Value(_key); value != nullptr && !Neuron::ParseInt64(*value, _out))
  {
    Malformed(_key, "a whole number");
  }
}

void SaveRecord::Read(std::string_view _key, std::uint64_t& _out)
{
  if (const std::string* value = Value(_key); value != nullptr && !Neuron::ParseUInt64(*value, _out))
  {
    Malformed(_key, "a whole number from 0");
  }
}

void SaveRecord::Read(std::string_view _key, std::int32_t& _out)
{
  if (const std::string* value = Value(_key); value != nullptr && !Neuron::ParseInt(*value, _out))
  {
    Malformed(_key, "a whole number");
  }
}

void SaveRecord::Read(std::string_view _key, std::vector<Neuron::CellSpan>& _out)
{
  const std::string* value = Value(_key);
  if (value == nullptr)
  {
    return;
  }
  std::vector<Neuron::CellSpan> spans;
  std::string_view rest = *value;
  while (!rest.empty())
  {
    const std::size_t comma = rest.find(',');
    const std::string_view item = rest.substr(0, comma);
    rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
    const std::size_t colon = item.find(':');
    std::uint64_t start = 0;
    std::uint64_t count = 0;
    if (colon == std::string_view::npos || !Neuron::ParseUInt64(item.substr(0, colon), start) ||
        !Neuron::ParseUInt64(item.substr(colon + 1), count) || start > 0xFFFFFFFFull || count == 0 || count > 0xFFFFFFFFull ||
        (!spans.empty() && start < static_cast<std::uint64_t>(spans.back().start) + spans.back().count))
    {
      Malformed(_key, "sorted, apart cell spans as start:count,...");
      return;
    }
    spans.push_back(Neuron::CellSpan{static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(count)});
  }
  _out = std::move(spans);
}

void SaveRecord::Read(std::string_view _key, std::span<std::int32_t> _out)
{
  const std::string* value = Value(_key);
  if (value == nullptr)
  {
    return;
  }
  std::string_view rest = *value;
  for (std::size_t i = 0; i < _out.size(); ++i)
  {
    const std::size_t comma = rest.find(',');
    if ((comma == std::string_view::npos) != (i + 1 == _out.size()) || !Neuron::ParseInt(rest.substr(0, comma), _out[i]))
    {
      Malformed(_key, std::to_string(_out.size()) + " whole numbers");
      return;
    }
    rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
  }
}

void SaveRecord::Read(std::string_view _key, bool& _out)
{
  if (const std::string* value = Value(_key))
  {
    if (*value == "1")
    {
      _out = true;
    }
    else if (*value == "0")
    {
      _out = false;
    }
    else
    {
      Malformed(_key, "0 or 1");
    }
  }
}

void SaveRecord::Read(std::string_view _key, UniversePosition& _out)
{
  if (const std::string* value = Value(_key))
  {
    double parts[3] = {};
    if (!ReadList(std::string_view(*value), parts, 3,
                  [](std::string_view _text, double& _part) { return Neuron::ParseDouble(_text, _part); }))
    {
      Malformed(_key, "three numbers");
      return;
    }
    _out = UniversePosition{parts[0], parts[1], parts[2]};
  }
}

void SaveRecord::Read(std::string_view _key, DirectX::XMFLOAT3& _out)
{
  if (const std::string* value = Value(_key))
  {
    float parts[3] = {};
    if (!ReadList(std::string_view(*value), parts, 3,
                  [](std::string_view _text, float& _part) { return Neuron::ParseFloat(_text, _part); }))
    {
      Malformed(_key, "three numbers");
      return;
    }
    _out = DirectX::XMFLOAT3{parts[0], parts[1], parts[2]};
  }
}

void SaveRecord::Read(std::string_view _key, DirectX::XMFLOAT4& _out)
{
  if (const std::string* value = Value(_key))
  {
    float parts[4] = {};
    if (!ReadList(std::string_view(*value), parts, 4,
                  [](std::string_view _text, float& _part) { return Neuron::ParseFloat(_text, _part); }))
    {
      Malformed(_key, "four numbers");
      return;
    }
    _out = DirectX::XMFLOAT4{parts[0], parts[1], parts[2], parts[3]};
  }
}

} // namespace Outpost
