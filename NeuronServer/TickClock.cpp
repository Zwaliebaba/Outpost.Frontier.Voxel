#include "TickClock.h"

#include <algorithm>

namespace Neuron
{

std::uint32_t TickClock::Advance(std::uint64_t _elapsedMicroseconds) noexcept
{
  m_pendingScaledMicroseconds += _elapsedMicroseconds * TICKS_PER_SECOND;
  const std::uint64_t due = m_pendingScaledMicroseconds / MICROSECONDS_PER_SECOND;
  const auto ticks = static_cast<std::uint32_t>(std::min<std::uint64_t>(due, MAX_TICKS_PER_ADVANCE));
  m_pendingScaledMicroseconds = ticks < due ? 0 : m_pendingScaledMicroseconds - due * MICROSECONDS_PER_SECOND;
  m_tickCount += ticks;
  return ticks;
}

float TickClock::Interpolation() const noexcept
{
  return static_cast<float>(m_pendingScaledMicroseconds) / static_cast<float>(MICROSECONDS_PER_SECOND);
}

} // namespace Neuron
