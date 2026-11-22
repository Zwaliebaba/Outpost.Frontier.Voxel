#include "Prediction.h"

#include <algorithm>
#include <utility>

using namespace DirectX;

namespace Outpost
{

namespace
{

[[nodiscard]] Pose PoseOf(const FlightState& _state) noexcept
{
  return Pose{_state.position, _state.orientation};
}

} // namespace

Pose BlendPoses(const Pose& _from, const Pose& _to, double _share) noexcept
{
  const double t = std::clamp(_share, 0.0, 1.0);
  Pose pose;
  pose.position =
    UniversePosition{_from.position.x + (_to.position.x - _from.position.x) * t, _from.position.y + (_to.position.y - _from.position.y) * t,
                     _from.position.z + (_to.position.z - _from.position.z) * t};
  XMStoreFloat4(&pose.orientation, XMQuaternionNormalize(XMQuaternionSlerp(XMLoadFloat4(&_from.orientation), XMLoadFloat4(&_to.orientation),
                                                                           static_cast<float>(t))));
  return pose;
}

void Prediction::Begin(const ShipBody& _body, std::uint64_t _tick, const FlightState& _state)
{
  m_body = _body;
  m_history.clear();
  m_state = _state;
  m_previous = _state;
  m_last = ShipTickResult{};
  m_tick = _tick;
  m_active = true;
}

void Prediction::Stop() noexcept
{
  m_active = false;
  m_history.clear();
}

void Prediction::SetBodies(std::vector<CollisionBox> _bodies)
{
  m_bodies = std::move(_bodies);
}

const ShipTickResult& Prediction::Step(const ShipCommand& _command, std::uint32_t _ticksPerSecond)
{
  m_previous = m_state;
  m_last = StepShip(m_body, _command, m_bodies, _ticksPerSecond, m_state);
  ++m_tick;
  m_history.push_back(Entry{m_tick, _command, m_state});
  while (m_history.size() > MAX_HISTORY_TICKS)
  {
    m_history.pop_front();
  }
  return m_last;
}

bool Prediction::Rebase(std::uint64_t _tick, const FlightState& _state, std::uint32_t _ticksPerSecond)
{
  // The server's word for a tick the client has not reached is simply where to start.
  if (!m_active || _tick >= m_tick)
  {
    const bool changed = !m_active || _tick > m_tick || !(_state == m_state);
    m_history.clear();
    m_state = _state;
    m_previous = _state;
    m_tick = _tick;
    m_corrections += changed ? 1 : 0;
    return changed;
  }
  while (!m_history.empty() && m_history.front().tick < _tick)
  {
    m_history.pop_front();
  }
  const bool agreed = !m_history.empty() && m_history.front().tick == _tick && m_history.front().after == _state;
  if (!m_history.empty() && m_history.front().tick == _tick)
  {
    m_history.pop_front();
  }
  if (agreed)
  {
    return false;
  }
  // Fly the ticks after it again on the commands they were flown on.
  m_state = _state;
  m_previous = _state;
  for (Entry& entry : m_history)
  {
    m_previous = m_state;
    m_last = StepShip(m_body, entry.command, m_bodies, _ticksPerSecond, m_state);
    entry.after = m_state;
  }
  ++m_corrections;
  return true;
}

const FlightState* Prediction::StateAt(std::uint64_t _tick) const noexcept
{
  if (_tick == m_tick)
  {
    return &m_state;
  }
  const auto found = std::ranges::find(m_history, _tick, &Entry::tick);
  return found != m_history.end() ? &found->after : nullptr;
}

Pose Prediction::Interpolated(float _alpha) const noexcept
{
  return BlendPoses(PoseOf(m_previous), PoseOf(m_state), static_cast<double>(_alpha));
}

void PoseInterpolation::Update(const UniverseSnapshot& _universe)
{
  if (_universe.posesRevision == m_seenRevision && m_tracks.size() == _universe.objects.size())
  {
    return;
  }
  m_seenRevision = _universe.posesRevision;
  for (const UniverseObject& object : _universe.objects)
  {
    auto [found, inserted] = m_tracks.try_emplace(object.id);
    Track& track = found->second;
    if (inserted)
    {
      track = Track{object.pose, object.poseTick, object.pose, object.poseTick};
    }
    else if (object.poseTick != track.currentTick)
    {
      track.previous = track.current;
      track.previousTick = track.currentTick;
      track.current = object.pose;
      track.currentTick = object.poseTick;
    }
  }
}

Pose PoseInterpolation::At(const UniverseObject& _object, double _tick) const noexcept
{
  const auto found = m_tracks.find(_object.id);
  if (found == m_tracks.end())
  {
    return _object.pose;
  }
  const Track& track = found->second;
  if (track.currentTick <= track.previousTick)
  {
    return track.current;
  }
  const double span = static_cast<double>(track.currentTick - track.previousTick);
  return BlendPoses(track.previous, track.current, (_tick - static_cast<double>(track.previousTick)) / span);
}

} // namespace Outpost
