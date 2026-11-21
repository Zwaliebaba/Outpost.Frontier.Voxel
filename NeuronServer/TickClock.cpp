#include "TickClock.h"

#include <algorithm>

namespace Neuron
{

std::uint32_t TickClock::Advance(std::uint64_t _elapsedMicroseconds) noexcept
{
  m_accumulatedMicroseconds += _elapsedMicroseconds;
  const std::uint64_t due = m_accumulatedMicroseconds / MICROSECONDS_PER_TICK;
  const auto ticks = static_cast<std::uint32_t>(std::min<std::uint64_t>(due, MAX_TICKS_PER_ADVANCE));
  m_accumulatedMicroseconds = ticks < due ? 0 : m_accumulatedMicroseconds - due * MICROSECONDS_PER_TICK;
  m_tickCount += ticks;
  return ticks;
}

float TickClock::Interpolation() const noexcept
{
  return static_cast<float>(m_accumulatedMicroseconds) / static_cast<float>(MICROSECONDS_PER_TICK);
}

} // namespace Neuron
