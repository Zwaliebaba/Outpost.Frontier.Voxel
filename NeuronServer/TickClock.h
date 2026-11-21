#pragma once

#include <cstdint>

namespace Neuron
{

/// Maps wall time to whole simulation ticks at one seam (AGENTS.md R16): the simulation never
/// reads a clock, it is told how many ticks to run.
class TickClock
{
public:
  static constexpr std::uint32_t TICKS_PER_SECOND = 20;
  static constexpr std::uint64_t MICROSECONDS_PER_TICK = 1'000'000 / TICKS_PER_SECOND;
  static constexpr std::uint32_t MAX_TICKS_PER_ADVANCE = 5; // after a stall, catch up gently instead of spiralling

  /// Adds elapsed wall time and returns how many ticks are now due.
  [[nodiscard]] std::uint32_t Advance(std::uint64_t _elapsedMicroseconds) noexcept;

  [[nodiscard]] std::uint64_t TickCount() const noexcept
  {
    return m_tickCount;
  }

  /// How far wall time is between the last tick and the next, 0..1, for presentation only.
  [[nodiscard]] float Interpolation() const noexcept;

private:
  std::uint64_t m_accumulatedMicroseconds = 0;
  std::uint64_t m_tickCount = 0;
};

} // namespace Neuron
