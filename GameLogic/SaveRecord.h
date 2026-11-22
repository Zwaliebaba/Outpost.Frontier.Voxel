#pragma once

#include "RecordFile.h"
#include "WorldObject.h"

#include <DirectXMath.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace Outpost
{

// Values as a save writes them (ADR-011): numbers in the shortest form that reads back to the same
// bits, so that a state restored from a save equals the state saved, field by field.

[[nodiscard]] std::string SaveText(float _value);
[[nodiscard]] std::string SaveText(double _value);
[[nodiscard]] std::string SaveText(std::int64_t _value);
[[nodiscard]] std::string SaveText(std::uint64_t _value);
[[nodiscard]] std::string SaveText(int _value);
[[nodiscard]] std::string SaveText(bool _value);
[[nodiscard]] std::string SaveText(const WorldPosition& _value);
[[nodiscard]] std::string SaveText(const DirectX::XMFLOAT3& _value);
[[nodiscard]] std::string SaveText(const DirectX::XMFLOAT4& _value);

/// Reads one record of a save section: only the keys it allows, each required, and the first
/// failure is kept as "<section> line <n>: <what>".
class SaveRecord
{
public:
  SaveRecord(const Neuron::Record& _record, std::string_view _section, std::string& _error) noexcept
    : m_record(_record),
      m_section(_section),
      m_error(_error)
  {
  }

  [[nodiscard]] bool AllowOnly(std::initializer_list<std::string_view> _keys);
  void Read(std::string_view _key, std::string& _out);
  void Read(std::string_view _key, float& _out);
  void Read(std::string_view _key, double& _out);
  void Read(std::string_view _key, std::int64_t& _out);
  void Read(std::string_view _key, std::uint64_t& _out);
  void Read(std::string_view _key, std::int32_t& _out);
  void Read(std::string_view _key, bool& _out);
  void Read(std::string_view _key, WorldPosition& _out);
  void Read(std::string_view _key, DirectX::XMFLOAT3& _out);
  void Read(std::string_view _key, DirectX::XMFLOAT4& _out);
  [[nodiscard]] bool Fail(std::string_view _message);

  [[nodiscard]] bool Ok() const noexcept
  {
    return m_ok;
  }

private:
  [[nodiscard]] const std::string* Value(std::string_view _key);
  void Malformed(std::string_view _key, std::string_view _expected);

  const Neuron::Record& m_record;
  std::string_view m_section;
  std::string& m_error;
  bool m_ok = true;
};

} // namespace Outpost
