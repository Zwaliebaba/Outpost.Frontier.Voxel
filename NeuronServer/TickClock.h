#pragma once

#include <cstdint>

namespace Neuron
{

/// Maps wall time to whole simulation ticks at one seam (AGENTS.md R20): the simulation never
/// reads a clock, it is told how many ticks to run. The rate and the arithmetic are ADR-007's.
class TickClock
{
public:
  static constexpr std::uint32_t TICKS_PER_SECOND = 60;
  static constexpr std::uint64_t MICROSECONDS_PER_SECOND = 1'000'000;
  // After a stall, catch up at most this many ticks (a quarter of a second) and drop the rest,
  // rather than spiral trying to replay it.
  static constexpr std::uint32_t MAX_TICKS_PER_ADVANCE = TICKS_PER_SECOND / 4;

  /// Adds elapsed wall time and returns how many ticks are now due.
  [[nodiscard]] std::uint32_t Advance(std::uint64_t _elapsedMicroseconds) noexcept;

  [[nodiscard]] std::uint64_t TickCount() const noexcept
  {
    return m_tickCount;
  }

  /// How far wall time is between the last tick and the next, 0..1, for presentation only.
  [[nodiscard]] float Interpolation() const noexcept;

private:
  // Elapsed microseconds not yet turned into ticks, multiplied by TICKS_PER_SECOND, so that a tick
  // is due per MICROSECONDS_PER_SECOND of it. A tick at 60 Hz is 16,666.67 microseconds; keeping
  // the remainder in these units makes the count exact instead of drifting by the truncated third.
  std::uint64_t m_pendingScaledMicroseconds = 0;
  std::uint64_t m_tickCount = 0;
};

} // namespace Neuron
