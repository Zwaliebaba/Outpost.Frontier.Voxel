#pragma once

#include "RecordFile.h"

#include <DirectXMath.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>

namespace Outpost
{

/// Reads one record's fields against the keys its keyword allows, so that a misspelt key is an
/// error rather than a silently ignored default (ADR-009).
class RecordReader
{
public:
  RecordReader(const Neuron::Record& _record, std::string_view _fileName, std::string& _error) noexcept
    : m_record(_record),
      m_fileName(_fileName),
      m_error(_error)
  {
  }

  [[nodiscard]] bool Fail(std::string_view _message)
  {
    if (m_ok)
    {
      m_error = std::string(m_fileName) + " line " + std::to_string(m_record.line) + ": " + std::string(_message);
    }
    m_ok = false;
    return false;
  }

  [[nodiscard]] bool AllowOnly(std::initializer_list<std::string_view> _keys)
  {
    for (const Neuron::RecordField& field : m_record.fields)
    {
      if (std::ranges::find(_keys, std::string_view(field.key)) == _keys.end())
      {
        return Fail("unknown key '" + field.key + "'");
      }
    }
    return m_ok;
  }

  void Name(std::string& _out)
  {
    if (!m_ok)
    {
      return;
    }
    if (!m_record.GetString("name", _out))
    {
      static_cast<void>(Fail("missing 'name'"));
      return;
    }
    const bool valid =
      !_out.empty() && _out[0] >= 'A' && _out[0] <= 'Z' &&
      std::ranges::all_of(_out, [](char _c) { return (_c >= 'A' && _c <= 'Z') || (_c >= 'a' && _c <= 'z') || (_c >= '0' && _c <= '9'); });
    if (!valid)
    {
      static_cast<void>(Fail("name '" + _out + "' must start with an upper-case letter and hold only letters and digits"));
    }
  }

  void Reference(std::string_view _key, std::string& _out)
  {
    if (m_ok && !m_record.GetString(_key, _out))
    {
      static_cast<void>(Fail("missing '" + std::string(_key) + "'"));
    }
  }

  /// A required number within [_minimum, _maximum].
  void Number(std::string_view _key, float _minimum, float _maximum, float& _out)
  {
    if (!m_ok)
    {
      return;
    }
    if (m_record.Find(_key) == nullptr)
    {
      static_cast<void>(Fail("missing '" + std::string(_key) + "'"));
    }
    else if (!m_record.GetFloat(_key, _out) || !(_out >= _minimum && _out <= _maximum))
    {
      static_cast<void>(
        Fail("'" + std::string(_key) + "' must be a number from " + std::to_string(_minimum) + " to " + std::to_string(_maximum)));
    }
  }

  /// A required triple, each component within [_minimum, _maximum].
  void Vector(std::string_view _key, float _minimum, float _maximum, DirectX::XMFLOAT3& _out)
  {
    if (!m_ok)
    {
      return;
    }
    if (m_record.Find(_key) == nullptr)
    {
      static_cast<void>(Fail("missing '" + std::string(_key) + "'"));
      return;
    }
    const auto inRange = [_minimum, _maximum](float _value) { return _value >= _minimum && _value <= _maximum; };
    if (!m_record.GetFloat3(_key, _out) || !inRange(_out.x) || !inRange(_out.y) || !inRange(_out.z))
    {
      static_cast<void>(
        Fail("'" + std::string(_key) + "' must be three numbers from " + std::to_string(_minimum) + " to " + std::to_string(_maximum)));
    }
  }

  /// A number within [_minimum, _maximum] when the key is given; otherwise _out keeps its default.
  void OptionalNumber(std::string_view _key, float _minimum, float _maximum, float& _out)
  {
    if (m_ok && m_record.Find(_key) != nullptr)
    {
      Number(_key, _minimum, _maximum, _out);
    }
  }

  /// A triple when the key is given; otherwise _out keeps its default.
  void OptionalVector(std::string_view _key, float _minimum, float _maximum, DirectX::XMFLOAT3& _out)
  {
    if (m_ok && m_record.Find(_key) != nullptr)
    {
      Vector(_key, _minimum, _maximum, _out);
    }
  }

  /// A string when the key is given; otherwise _out keeps its default.
  void OptionalString(std::string_view _key, std::string& _out)
  {
    if (m_ok && m_record.Find(_key) != nullptr)
    {
      Reference(_key, _out);
    }
  }

  [[nodiscard]] const Neuron::Record& Source() const noexcept
  {
    return m_record;
  }

  [[nodiscard]] bool Ok() const noexcept
  {
    return m_ok;
  }

private:
  const Neuron::Record& m_record;
  std::string_view m_fileName;
  std::string& m_error;
  bool m_ok = true;
};

} // namespace Outpost
