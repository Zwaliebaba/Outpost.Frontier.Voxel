#pragma once

#include <DirectXMath.h>

#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

// A line-oriented text format for hand-edited data (ADR-002):
//
//   # a comment runs to the end of the line
//   keyword key=value key="quoted value" key=1.5,2,3
//
// The first token is the record's keyword; every other token is key=value. A key may repeat, and
// order is kept. The format knows nothing about what the keywords mean: the game layer does.

struct RecordField
{
  std::string key;
  std::string value;
};

struct Record
{
  std::string keyword;
  std::vector<RecordField> fields;
  int line = 0;

  [[nodiscard]] const std::string* Find(std::string_view _key) const noexcept;
  [[nodiscard]] std::vector<const std::string*> FindAll(std::string_view _key) const;

  [[nodiscard]] bool GetString(std::string_view _key, std::string& _out) const;
  [[nodiscard]] bool GetFloat(std::string_view _key, float& _out) const;
  [[nodiscard]] bool GetInt(std::string_view _key, int& _out) const;
  [[nodiscard]] bool GetFloat3(std::string_view _key, DirectX::XMFLOAT3& _out) const;
  [[nodiscard]] bool GetBool(std::string_view _key, bool& _out) const;
};

[[nodiscard]] bool ParseRecords(std::string_view _text, std::vector<Record>& _outRecords, std::string& _outError);

/// Locale-independent number parsing shared by the record accessors.
[[nodiscard]] bool ParseFloat(std::string_view _text, float& _out) noexcept;
[[nodiscard]] bool ParseInt(std::string_view _text, int& _out) noexcept;
[[nodiscard]] bool ParseFloatList(std::string_view _text, float* _out, int _count) noexcept;

} // namespace Neuron
