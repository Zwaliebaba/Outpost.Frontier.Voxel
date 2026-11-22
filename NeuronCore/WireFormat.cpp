#include "WireFormat.h"

#include <bit>

namespace Neuron
{

namespace
{

void AppendLittleEndian(std::vector<std::uint8_t>& _bytes, std::uint64_t _value, std::size_t _byteCount)
{
  for (std::size_t i = 0; i < _byteCount; ++i)
  {
    _bytes.push_back(static_cast<std::uint8_t>(_value >> (8 * i)));
  }
}

} // namespace

void WireWriter::Uint8(std::uint8_t _value)
{
  m_bytes.push_back(_value);
}

void WireWriter::Uint16(std::uint16_t _value)
{
  AppendLittleEndian(m_bytes, _value, 2);
}

void WireWriter::Uint32(std::uint32_t _value)
{
  AppendLittleEndian(m_bytes, _value, 4);
}

void WireWriter::Uint64(std::uint64_t _value)
{
  AppendLittleEndian(m_bytes, _value, 8);
}

void WireWriter::Int32(std::int32_t _value)
{
  Uint32(static_cast<std::uint32_t>(_value));
}

void WireWriter::Int64(std::int64_t _value)
{
  Uint64(static_cast<std::uint64_t>(_value));
}

void WireWriter::Float32(float _value)
{
  Uint32(std::bit_cast<std::uint32_t>(_value));
}

void WireWriter::Float64(double _value)
{
  Uint64(std::bit_cast<std::uint64_t>(_value));
}

void WireWriter::String(std::string_view _text)
{
  if (_text.size() > WIRE_MAX_STRING_BYTES)
  {
    m_ok = false;
    return;
  }
  Uint16(static_cast<std::uint16_t>(_text.size()));
  m_bytes.insert(m_bytes.end(), _text.begin(), _text.end());
}

std::vector<std::uint8_t> WireWriter::TakeBytes() noexcept
{
  return std::move(m_bytes);
}

std::uint64_t WireReader::Unsigned(std::size_t _byteCount) noexcept
{
  if (!m_ok || Remaining() < _byteCount)
  {
    m_ok = false;
    return 0;
  }
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < _byteCount; ++i)
  {
    value |= std::uint64_t{m_bytes[m_offset + i]} << (8 * i);
  }
  m_offset += _byteCount;
  return value;
}

std::uint8_t WireReader::Uint8() noexcept
{
  return static_cast<std::uint8_t>(Unsigned(1));
}

std::uint16_t WireReader::Uint16() noexcept
{
  return static_cast<std::uint16_t>(Unsigned(2));
}

std::uint32_t WireReader::Uint32() noexcept
{
  return static_cast<std::uint32_t>(Unsigned(4));
}

std::uint64_t WireReader::Uint64() noexcept
{
  return Unsigned(8);
}

std::int32_t WireReader::Int32() noexcept
{
  return static_cast<std::int32_t>(Uint32());
}

std::int64_t WireReader::Int64() noexcept
{
  return static_cast<std::int64_t>(Uint64());
}

float WireReader::Float32() noexcept
{
  return std::bit_cast<float>(Uint32());
}

double WireReader::Float64() noexcept
{
  return std::bit_cast<double>(Uint64());
}

std::string WireReader::String()
{
  const std::size_t length = Uint16();
  if (!m_ok || Remaining() < length)
  {
    m_ok = false;
    return {};
  }
  const auto* first = m_bytes.data() + m_offset;
  m_offset += length;
  return std::string(reinterpret_cast<const char*>(first), length);
}

} // namespace Neuron
