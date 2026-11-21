#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

namespace NeuronCoreTests
{

[[nodiscard]] bool Near(float _a, float _b, float _tolerance = 1e-4f) noexcept;
[[nodiscard]] bool Near(const DirectX::XMFLOAT3& _a, const DirectX::XMFLOAT3& _b, float _tolerance = 1e-4f) noexcept;
[[nodiscard]] DirectX::XMFLOAT3 Row(DirectX::FXMMATRIX _matrix, int _row) noexcept;

/// Little-endian byte writer for hand-authored .vox fixtures.
class ByteWriter
{
public:
  void Tag(const char (&_tag)[5]);
  void Int(std::int32_t _value);
  void Byte(std::uint8_t _value)
  {
    m_bytes.push_back(_value);
  }
  [[nodiscard]] std::vector<std::uint8_t>& Bytes() noexcept
  {
    return m_bytes;
  }

private:
  std::vector<std::uint8_t> m_bytes;
};

} // namespace NeuronCoreTests
