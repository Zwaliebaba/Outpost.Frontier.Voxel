#include "Targeting.h"

#include <algorithm>
#include <vector>

namespace Outpost
{

namespace
{

[[nodiscard]] double DistanceSquared(const UniversePosition& _a, const UniversePosition& _b) noexcept
{
  const double x = _a.x - _b.x;
  const double y = _a.y - _b.y;
  const double z = _a.z - _b.z;
  return x * x + y * y + z * z;
}

} // namespace

void Targeting::Next(std::span<const TargetCandidate> _candidates, const UniversePosition& _from, const DirectX::XMFLOAT3& _forward)
{
  std::vector<const TargetCandidate*> hostiles;
  for (const TargetCandidate& candidate : _candidates)
  {
    if (candidate.ship && candidate.owner == Owner::Hostile)
    {
      hostiles.push_back(&candidate);
    }
  }
  if (hostiles.empty())
  {
    return;
  }
  // Nearest first, and by identity between equals, so that cycling visits each once.
  std::ranges::sort(hostiles,
                    [&](const TargetCandidate* _a, const TargetCandidate* _b)
                    {
                      const double a = DistanceSquared(_a->position, _from);
                      const double b = DistanceSquared(_b->position, _from);
                      return a != b ? a < b : _a->object < _b->object;
                    });
  const auto current = std::ranges::find(hostiles, m_target, &TargetCandidate::object);
  if (current != hostiles.end())
  {
    const auto next = current + 1 == hostiles.end() ? hostiles.begin() : current + 1;
    Set((*next)->object);
    return;
  }
  const auto ahead = std::ranges::find_if(
    hostiles,
    [&](const TargetCandidate* _candidate)
    {
      const double x = _candidate->position.x - _from.x;
      const double y = _candidate->position.y - _from.y;
      const double z = _candidate->position.z - _from.z;
      return x * static_cast<double>(_forward.x) + y * static_cast<double>(_forward.y) + z * static_cast<double>(_forward.z) > 0.0;
    });
  Set((ahead != hostiles.end() ? *ahead : hostiles.front())->object);
}

void Targeting::Set(ObjectId _object) noexcept
{
  if (_object != m_target)
  {
    m_module = WHOLE_SHIP;
  }
  m_target = _object;
}

void Targeting::Clear() noexcept
{
  m_target = NO_OBJECT;
  m_module = WHOLE_SHIP;
}

void Targeting::CycleModule(int _step, std::size_t _moduleCount) noexcept
{
  if (m_target == NO_OBJECT || _moduleCount == 0)
  {
    m_module = WHOLE_SHIP;
    return;
  }
  // The whole ship, then each module: _moduleCount + 1 places, round.
  const int places = static_cast<int>(_moduleCount) + 1;
  const int place = (m_module + 1 + (_step < 0 ? places - 1 : 1)) % places;
  m_module = place - 1;
}

void Targeting::Keep(std::span<const TargetCandidate> _candidates) noexcept
{
  if (m_target != NO_OBJECT && std::ranges::find(_candidates, m_target, &TargetCandidate::object) == _candidates.end())
  {
    Clear();
  }
}

} // namespace Outpost
