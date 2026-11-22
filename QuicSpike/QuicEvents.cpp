#include "QuicEvents.h"

#include <utility>

namespace Outpost
{

void QuicEventQueue::Push(QuicEvent&& _event)
{
  const std::scoped_lock lock(m_mutex);
  m_events.push_back(std::move(_event));
}

std::optional<QuicEvent> QuicEventQueue::Pop()
{
  const std::scoped_lock lock(m_mutex);
  if (m_events.empty())
  {
    return std::nullopt;
  }
  QuicEvent event = std::move(m_events.front());
  m_events.pop_front();
  return event;
}

} // namespace Outpost
