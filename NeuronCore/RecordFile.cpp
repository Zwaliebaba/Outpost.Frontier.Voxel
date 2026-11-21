#include "RecordFile.h"

#include <charconv>

namespace Neuron
{

namespace
{

[[nodiscard]] bool IsSpace(char _c) noexcept
{
  return _c == ' ' || _c == '\t' || _c == '\r';
}

[[nodiscard]] std::string_view Trim(std::string_view _text) noexcept
{
  while (!_text.empty() && IsSpace(_text.front()))
  {
    _text.remove_prefix(1);
  }
  while (!_text.empty() && IsSpace(_text.back()))
  {
    _text.remove_suffix(1);
  }
  return _text;
}

// Splits one line into tokens. A double-quoted run is part of a token and may contain spaces;
// the quotes themselves are dropped.
[[nodiscard]] bool Tokenize(std::string_view _line, std::vector<std::string>& _out, std::string& _error)
{
  _out.clear();
  std::size_t at = 0;
  while (at < _line.size())
  {
    while (at < _line.size() && IsSpace(_line[at]))
    {
      ++at;
    }
    if (at >= _line.size() || _line[at] == '#')
    {
      break;
    }
    std::string token;
    while (at < _line.size() && !IsSpace(_line[at]))
    {
      if (_line[at] == '"')
      {
        const std::size_t close = _line.find('"', at + 1);
        if (close == std::string_view::npos)
        {
          _error = "unterminated quote";
          return false;
        }
        token.append(_line.substr(at + 1, close - at - 1));
        at = close + 1;
      }
      else
      {
        token.push_back(_line[at]);
        ++at;
      }
    }
    _out.push_back(std::move(token));
  }
  return true;
}

} // namespace

bool ParseFloat(std::string_view _text, float& _out) noexcept
{
  _text = Trim(_text);
  if (!_text.empty() && _text.front() == '+')
  {
    _text.remove_prefix(1);
  }
  const auto result = std::from_chars(_text.data(), _text.data() + _text.size(), _out);
  return result.ec == std::errc{} && result.ptr == _text.data() + _text.size();
}

bool ParseInt(std::string_view _text, int& _out) noexcept
{
  _text = Trim(_text);
  const auto result = std::from_chars(_text.data(), _text.data() + _text.size(), _out);
  return result.ec == std::errc{} && result.ptr == _text.data() + _text.size();
}

bool ParseFloatList(std::string_view _text, float* _out, int _count) noexcept
{
  for (int i = 0; i < _count; ++i)
  {
    const std::size_t comma = _text.find(',');
    const bool last = i + 1 == _count;
    if (last != (comma == std::string_view::npos))
    {
      return false;
    }
    if (!ParseFloat(last ? _text : _text.substr(0, comma), _out[i]))
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

const std::string* Record::Find(std::string_view _key) const noexcept
{
  for (const RecordField& field : fields)
  {
    if (field.key == _key)
    {
      return &field.value;
    }
  }
  return nullptr;
}

std::vector<const std::string*> Record::FindAll(std::string_view _key) const
{
  std::vector<const std::string*> values;
  for (const RecordField& field : fields)
  {
    if (field.key == _key)
    {
      values.push_back(&field.value);
    }
  }
  return values;
}

bool Record::GetString(std::string_view _key, std::string& _out) const
{
  const std::string* value = Find(_key);
  if (value == nullptr)
  {
    return false;
  }
  _out = *value;
  return true;
}

bool Record::GetFloat(std::string_view _key, float& _out) const
{
  const std::string* value = Find(_key);
  return value != nullptr && ParseFloat(*value, _out);
}

bool Record::GetInt(std::string_view _key, int& _out) const
{
  const std::string* value = Find(_key);
  return value != nullptr && ParseInt(*value, _out);
}

bool Record::GetFloat3(std::string_view _key, DirectX::XMFLOAT3& _out) const
{
  const std::string* value = Find(_key);
  float components[3] = {};
  if (value == nullptr || !ParseFloatList(*value, components, 3))
  {
    return false;
  }
  _out = DirectX::XMFLOAT3{components[0], components[1], components[2]};
  return true;
}

bool Record::GetBool(std::string_view _key, bool& _out) const
{
  const std::string* value = Find(_key);
  if (value == nullptr)
  {
    return false;
  }
  if (*value == "true" || *value == "1" || *value == "yes")
  {
    _out = true;
    return true;
  }
  if (*value == "false" || *value == "0" || *value == "no")
  {
    _out = false;
    return true;
  }
  return false;
}

bool ParseRecords(std::string_view _text, std::vector<Record>& _outRecords, std::string& _outError)
{
  _outRecords.clear();
  _outError.clear();
  int lineNumber = 0;
  std::vector<std::string> tokens;
  while (!_text.empty())
  {
    ++lineNumber;
    const std::size_t newline = _text.find('\n');
    const std::string_view line = newline == std::string_view::npos ? _text : _text.substr(0, newline);
    _text.remove_prefix(newline == std::string_view::npos ? _text.size() : newline + 1);

    std::string tokenError;
    if (!Tokenize(line, tokens, tokenError))
    {
      _outError = "line " + std::to_string(lineNumber) + ": " + tokenError;
      return false;
    }
    if (tokens.empty())
    {
      continue;
    }
    Record record;
    record.keyword = tokens.front();
    record.line = lineNumber;
    for (std::size_t i = 1; i < tokens.size(); ++i)
    {
      const std::size_t equals = tokens[i].find('=');
      if (equals == std::string::npos || equals == 0)
      {
        _outError = "line " + std::to_string(lineNumber) + ": expected key=value, found '" + tokens[i] + "'";
        return false;
      }
      record.fields.push_back(RecordField{tokens[i].substr(0, equals), tokens[i].substr(equals + 1)});
    }
    _outRecords.push_back(std::move(record));
  }
  return true;
}

} // namespace Neuron
