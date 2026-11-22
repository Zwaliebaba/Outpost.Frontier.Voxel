#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

// The byte order and field widths of everything that crosses between client and server
// (ADR-008): little-endian, fixed widths, strings as a 16-bit byte count and UTF-8 bytes. The
// same bytes on every machine, whatever the host's own order.

inline constexpr std::size_t WIRE_MAX_STRING_BYTES = 65'535;

/// Appends values in the wire format to a growing buffer.
class WireWriter
{
public:
  void Uint8(std::uint8_t _value);
  void Uint16(std::uint16_t _value);
  void Uint32(std::uint32_t _value);
  void Uint64(std::uint64_t _value);
  void Int32(std::int32_t _value);
  void Int64(std::int64_t _value);
  void Float32(float _value);
  void Float64(double _value);
  /// A string longer than WIRE_MAX_STRING_BYTES is not written and marks the writer failed.
  void String(std::string_view _text);

  /// For a caller that finds a value it cannot represent: the bytes are then not to be sent.
  void MarkFailed() noexcept
  {
    m_ok = false;
  }

  /// False once anything could not be written; the bytes are then not to be sent.
  [[nodiscard]] bool Ok() const noexcept
  {
    return m_ok;
  }
  [[nodiscard]] const std::vector<std::uint8_t>& Bytes() const noexcept
  {
    return m_bytes;
  }
  [[nodiscard]] std::vector<std::uint8_t> TakeBytes() noexcept;

private:
  std::vector<std::uint8_t> m_bytes;
  bool m_ok = true;
};

/// Reads values in the wire format. Reading past the end yields zero and marks the reader failed,
/// so a decoder reads every field and checks Ok() once at the end.
class WireReader
{
public:
  explicit WireReader(std::span<const std::uint8_t> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  [[nodiscard]] std::uint8_t Uint8() noexcept;
  [[nodiscard]] std::uint16_t Uint16() noexcept;
  [[nodiscard]] std::uint32_t Uint32() noexcept;
  [[nodiscard]] std::uint64_t Uint64() noexcept;
  [[nodiscard]] std::int32_t Int32() noexcept;
  [[nodiscard]] std::int64_t Int64() noexcept;
  [[nodiscard]] float Float32() noexcept;
  [[nodiscard]] double Float64() noexcept;
  [[nodiscard]] std::string String();

  [[nodiscard]] bool Ok() const noexcept
  {
    return m_ok;
  }
  [[nodiscard]] std::size_t Remaining() const noexcept
  {
    return m_bytes.size() - m_offset;
  }

private:
  [[nodiscard]] std::uint64_t Unsigned(std::size_t _byteCount) noexcept;

  std::span<const std::uint8_t> m_bytes;
  std::size_t m_offset = 0;
  bool m_ok = true;
};

} // namespace Neuron
