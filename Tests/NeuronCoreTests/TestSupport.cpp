#include "TestSupport.h"

#include <cmath>

namespace NeuronCoreTests
{

bool Near(float _a, float _b, float _tolerance) noexcept
{
  return std::abs(_a - _b) <= _tolerance;
}

bool Near(const DirectX::XMFLOAT3& _a, const DirectX::XMFLOAT3& _b, float _tolerance) noexcept
{
  return Near(_a.x, _b.x, _tolerance) && Near(_a.y, _b.y, _tolerance) && Near(_a.z, _b.z, _tolerance);
}

DirectX::XMFLOAT3 Row(DirectX::FXMMATRIX _matrix, int _row) noexcept
{
  DirectX::XMFLOAT3 row{};
  DirectX::XMStoreFloat3(&row, _matrix.r[_row]);
  return row;
}

void ByteWriter::Tag(const char (&_tag)[5])
{
  for (int i = 0; i < 4; ++i)
  {
    m_bytes.push_back(static_cast<std::uint8_t>(_tag[i]));
  }
}

void ByteWriter::Int(std::int32_t _value)
{
  const auto value = static_cast<std::uint32_t>(_value);
  for (int shift = 0; shift < 32; shift += 8)
  {
    m_bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

} // namespace NeuronCoreTests
