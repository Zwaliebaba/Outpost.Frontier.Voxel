#include "Combat.h"

#include "ModelAxes.h"
#include "Palette.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace DirectX;

namespace Outpost
{

namespace
{

[[nodiscard]] XMFLOAT3 Offset(const UniversePosition& _from, const UniversePosition& _to) noexcept
{
  return XMFLOAT3{static_cast<float>(_to.x - _from.x), static_cast<float>(_to.y - _from.y), static_cast<float>(_to.z - _from.z)};
}

/// Where a segment from _start along unit _direction first meets a sphere, within _length, or
/// nothing. A segment that starts inside it meets it at once.
[[nodiscard]] std::optional<float> MeetSphere(const UniversePosition& _center, float _radius, const UniversePosition& _start,
                                              const XMFLOAT3& _direction, float _length) noexcept
{
  const XMFLOAT3 offset = Offset(_center, _start);
  const XMVECTOR s = XMLoadFloat3(&offset);
  const XMVECTOR d = XMLoadFloat3(&_direction);
  const float b = XMVectorGetX(XMVector3Dot(s, d));
  const float c = XMVectorGetX(XMVector3Dot(s, s)) - _radius * _radius;
  if (c <= 0.0f)
  {
    return 0.0f;
  }
  const float discriminant = b * b - c;
  if (b > 0.0f || discriminant < 0.0f)
  {
    return std::nullopt;
  }
  const float t = -b - std::sqrt(discriminant);
  return t <= _length ? std::optional<float>(std::max(0.0f, t)) : std::nullopt;
}

} // namespace

std::optional<CellHit> TraceMask(const Neuron::VoxelMask& _mask, const Pose& _pose, float _scale, const XMFLOAT3& _pivotVox,
                                 float _voxelSize, const UniversePosition& _start, const XMFLOAT3& _direction, float _lengthMetres) noexcept
{
  const float metresPerCell = _scale * _voxelSize;
  if (!(metresPerCell > 0.0f))
  {
    return std::nullopt;
  }
  const XMVECTOR inverse = XMQuaternionConjugate(XMLoadFloat4(&_pose.orientation));
  const XMFLOAT3 offset = Offset(_pose.position, _start);
  XMFLOAT3 local{};
  XMStoreFloat3(&local, XMVectorScale(XMVector3Rotate(XMLoadFloat3(&offset), inverse), 1.0f / _scale));
  XMFLOAT3 dir{};
  XMStoreFloat3(&dir, XMVector3Rotate(XMLoadFloat3(&_direction), inverse));
  // Model space to VOX lattice: the inverse of ModelAxes.h's swap of Y and Z, about the pivot.
  const std::array<float, 3> origin{local.x / _voxelSize + _pivotVox.x, local.z / _voxelSize + _pivotVox.y,
                                    local.y / _voxelSize + _pivotVox.z};
  const std::array<float, 3> step{dir.x, dir.z, dir.y};
  const std::array<float, 3> size{static_cast<float>(_mask.Size().x), static_cast<float>(_mask.Size().y),
                                  static_cast<float>(_mask.Size().z)};
  const float length = _lengthMetres / metresPerCell;

  // The segment against the grid's box, by slabs.
  float enter = 0.0f;
  float leave = length;
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    if (std::abs(step[axis]) < 1.0e-9f)
    {
      if (origin[axis] < 0.0f || origin[axis] >= size[axis])
      {
        return std::nullopt;
      }
      continue;
    }
    float a = (0.0f - origin[axis]) / step[axis];
    float b = (size[axis] - origin[axis]) / step[axis];
    if (a > b)
    {
      std::swap(a, b);
    }
    enter = std::max(enter, a);
    leave = std::min(leave, b);
    if (enter > leave)
    {
      return std::nullopt;
    }
  }

  // Amanatides and Woo's walk, cell by cell, from where the segment enters the grid.
  const float nudge = 1.0e-4f;
  std::array<int, 3> cell{};
  std::array<int, 3> stride{};
  std::array<float, 3> next{};
  std::array<float, 3> delta{};
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    const float at = origin[axis] + step[axis] * (enter + nudge);
    cell[axis] = std::clamp(static_cast<int>(std::floor(at)), 0, static_cast<int>(size[axis]) - 1);
    if (step[axis] > 0.0f)
    {
      stride[axis] = 1;
      next[axis] = (static_cast<float>(cell[axis] + 1) - origin[axis]) / step[axis];
      delta[axis] = 1.0f / step[axis];
    }
    else if (step[axis] < 0.0f)
    {
      stride[axis] = -1;
      next[axis] = (static_cast<float>(cell[axis]) - origin[axis]) / step[axis];
      delta[axis] = -1.0f / step[axis];
    }
    else
    {
      stride[axis] = 0;
      next[axis] = std::numeric_limits<float>::infinity();
      delta[axis] = std::numeric_limits<float>::infinity();
    }
  }
  float t = enter;
  while (t <= leave)
  {
    if (_mask.Present(cell[0], cell[1], cell[2]))
    {
      CellHit hit;
      hit.distanceMetres = t * metresPerCell;
      hit.cell = Neuron::Int3{cell[0], cell[1], cell[2]};
      hit.directionVox = XMFLOAT3{step[0], step[1], step[2]};
      return hit;
    }
    const std::size_t axis = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
    t = next[axis];
    next[axis] += delta[axis];
    cell[axis] += stride[axis];
    if (cell[axis] < 0 || cell[axis] >= static_cast<int>(size[axis]))
    {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

void Combat::Load(std::shared_ptr<const CombatContent> _content, const SceneDefinition& _scene, const std::vector<UniverseObject>& _objects)
{
  m_content = std::move(_content);
  m_bodies.clear();
  m_bolts.clear();
  m_wrecks.clear();
  m_events.clear();
  if (m_content == nullptr)
  {
    return;
  }
  for (const UniverseObject& object : _objects)
  {
    const SceneInstance* instance = _scene.FindInstance(object.name);
    const ModelAsset* model = _scene.FindModel(object.model);
    const VoxelArchetype* archetype = m_content->FindArchetype(object.model);
    if (instance == nullptr || model == nullptr || archetype == nullptr || instance->style == InstanceStyle::Blueprint)
    {
      continue; // a build plan is not there yet, so nothing meets it
    }
    Body body;
    body.object = object.id;
    body.archetype = static_cast<std::size_t>(archetype - m_content->archetypes.data());
    body.initial = InitialMask(*m_content, *archetype, instance->preset);
    body.mask = body.initial;
    body.owner = object.owner;
    body.indestructible = instance->indestructible;
    body.pivotVox = model->pivotVox;
    body.voxelSize = model->voxelSize;
    m_bodies.push_back(std::move(body));
  }
}

void Combat::Fire(ObjectId _shooter, Owner _owner, const Shot& _shot, std::uint8_t _launcher, const ShipContent& _ships,
                  const XMFLOAT3& _shooterVelocity, std::uint32_t _ticksPerSecond, std::uint64_t _tick)
{
  if (_launcher >= _ships.launchers.size())
  {
    return;
  }
  const LauncherArchetype& launcher = _ships.launchers[_launcher];
  Bolt bolt;
  bolt.shooter = _shooter;
  bolt.owner = _owner;
  bolt.launcher = _launcher;
  bolt.position = _shot.origin;
  XMStoreFloat3(
    &bolt.velocityMetresPerSecond,
    XMVectorAdd(XMVectorScale(XMLoadFloat3(&_shot.direction), launcher.muzzleSpeedMetresPerSecond), XMLoadFloat3(&_shooterVelocity)));
  bolt.ticksLeft = std::max(1, static_cast<std::int32_t>(std::lround(launcher.lifetimeSeconds * static_cast<float>(_ticksPerSecond))));
  // The seed is the event's own (ADR-012): from the tick, the shooter and the mount, nothing else.
  bolt.seed = Neuron::HashCell(static_cast<std::uint32_t>(_tick), _shooter * 8u + _shot.mount);
  m_bolts.push_back(bolt);
}

std::vector<BoltHit> Combat::StepBolts(std::span<const CombatTarget> _targets, const ShipContent& _ships, std::uint32_t _ticksPerSecond,
                                       std::uint64_t _tick)
{
  static_cast<void>(_tick);
  std::vector<BoltHit> hits;
  const float dt = 1.0f / static_cast<float>(_ticksPerSecond);
  std::size_t kept = 0;
  for (std::size_t b = 0; b < m_bolts.size(); ++b)
  {
    Bolt bolt = m_bolts[b];
    if (bolt.launcher >= _ships.launchers.size() || bolt.ticksLeft <= 0)
    {
      continue;
    }
    const LauncherArchetype& launcher = _ships.launchers[bolt.launcher];
    const XMVECTOR velocity = XMLoadFloat3(&bolt.velocityMetresPerSecond);
    const float length = XMVectorGetX(XMVector3Length(velocity)) * dt;
    XMFLOAT3 direction{};
    XMStoreFloat3(&direction, XMVector3Normalize(velocity));

    // The first thing along the segment: a shield bubble that is up, or a present cell.
    float best = std::numeric_limits<float>::infinity();
    const CombatTarget* met = nullptr;
    bool shieldMet = false;
    std::optional<CellHit> cellMet;
    Body* bodyMet = nullptr;
    for (const CombatTarget& target : _targets)
    {
      if (target.object == bolt.shooter)
      {
        continue;
      }
      if (target.flight != nullptr && target.spec != nullptr && target.spec->shield.capacity > 0.0f && target.flight->shieldUp)
      {
        const std::optional<float> at = MeetSphere(target.pose.position, target.shieldRadiusMetres, bolt.position, direction, length);
        if (at.has_value() && *at < best)
        {
          best = *at;
          met = &target;
          shieldMet = true;
          cellMet.reset();
          bodyMet = nullptr;
          continue; // inside a bubble that is up, the hull cannot be reached
        }
      }
      Body* body = FindBody(target.object);
      if (body == nullptr)
      {
        continue;
      }
      // A cheap test first: the segment against the sphere around the whole grid.
      const Neuron::Int3& size = body->mask.Size();
      const float reach =
        0.5f * target.scale * body->voxelSize * std::sqrt(static_cast<float>(size.x * size.x + size.y * size.y + size.z * size.z)) +
        target.scale * body->voxelSize *
          std::sqrt(body->pivotVox.x * body->pivotVox.x + body->pivotVox.y * body->pivotVox.y + body->pivotVox.z * body->pivotVox.z);
      if (!MeetSphere(target.pose.position, reach, bolt.position, direction, length).has_value())
      {
        continue;
      }
      const std::optional<CellHit> cell =
        TraceMask(body->mask, target.pose, target.scale, body->pivotVox, body->voxelSize, bolt.position, direction, length);
      if (cell.has_value() && cell->distanceMetres < best)
      {
        best = cell->distanceMetres;
        met = &target;
        shieldMet = false;
        cellMet = cell;
        bodyMet = body;
      }
    }

    if (met == nullptr)
    {
      XMStoreFloat3(&direction, XMVectorScale(velocity, dt));
      bolt.position =
        UniversePosition{bolt.position.x + static_cast<double>(direction.x), bolt.position.y + static_cast<double>(direction.y),
                         bolt.position.z + static_cast<double>(direction.z)};
      --bolt.ticksLeft;
      if (bolt.ticksLeft > 0)
      {
        m_bolts[kept++] = bolt;
      }
      continue;
    }

    BoltHit hit;
    hit.shooter = bolt.shooter;
    hit.target = met->object;
    hit.shield = shieldMet;
    hit.harmful = !met->isProtected && Harms(bolt.owner, met->owner) && (bodyMet == nullptr || !bodyMet->indestructible);
    if (hit.harmful && shieldMet)
    {
      hit.shieldDamage = ShieldDamage(launcher.hullDamage, launcher.energyDamage);
    }
    else if (hit.harmful && bodyMet != nullptr && cellMet.has_value())
    {
      hit.energyDamage = launcher.energyDamage;
      MaskEvent event;
      event.target = bodyMet->object;
      event.kind = MaskEventKind::Carve;
      event.carve.sequence = bodyMet->nextEvent++;
      event.carve.cell = cellMet->cell;
      event.carve.direction = Neuron::Int3{static_cast<int>(std::lround(cellMet->directionVox.x * 64.0f)),
                                           static_cast<int>(std::lround(cellMet->directionVox.y * 64.0f)),
                                           static_cast<int>(std::lround(cellMet->directionVox.z * 64.0f))};
      event.carve.shape = launcher.carveShape;
      event.carve.radiusCells = launcher.carveRadiusCells;
      event.carve.lengthCells = launcher.carveLengthCells;
      event.carve.energy = launcher.carveEnergy;
      event.carve.seed = bolt.seed;
      event.toughened = bodyMet->owner == Owner::Player;
      event.cause = DamageCause::Weapon;
      event.inflictor = bolt.shooter;
      hit.carved = ApplyMaskEvent(m_content->archetypes[bodyMet->archetype], event, bodyMet->mask);
      Log(event);
      XMFLOAT3 targetVelocity{};
      if (met->flight != nullptr)
      {
        targetVelocity = met->flight->velocityMetresPerSecond;
      }
      SpawnWrecks(*bodyMet, hit.carved.detached, met->pose, met->scale, targetVelocity);
    }
    hits.push_back(std::move(hit));
  }
  m_bolts.resize(kept);
  return hits;
}

void Combat::StepWrecks(std::uint32_t _ticksPerSecond)
{
  const double dt = 1.0 / static_cast<double>(_ticksPerSecond);
  for (Wreck& wreck : m_wrecks)
  {
    wreck.pose.position.x += static_cast<double>(wreck.velocityMetresPerSecond.x) * dt;
    wreck.pose.position.y += static_cast<double>(wreck.velocityMetresPerSecond.y) * dt;
    wreck.pose.position.z += static_cast<double>(wreck.velocityMetresPerSecond.z) * dt;
  }
}

Combat::Body* Combat::FindBody(ObjectId _object) noexcept
{
  const auto found = std::ranges::find(m_bodies, _object, &Body::object);
  return found != m_bodies.end() ? &*found : nullptr;
}

const Combat::Body* Combat::FindBody(ObjectId _object) const noexcept
{
  const auto found = std::ranges::find(m_bodies, _object, &Body::object);
  return found != m_bodies.end() ? &*found : nullptr;
}

const HullRecord* Combat::HullOf(const Body& _body) const noexcept
{
  return m_content->HullOf(m_content->archetypes[_body.archetype]);
}

const Neuron::VoxelMask* Combat::Mask(ObjectId _object) const noexcept
{
  const Body* body = FindBody(_object);
  return body != nullptr ? &body->mask : nullptr;
}

const Neuron::VoxelMask* Combat::Initial(ObjectId _object) const noexcept
{
  const Body* body = FindBody(_object);
  return body != nullptr ? &body->initial : nullptr;
}

const VoxelArchetype* Combat::ArchetypeOf(ObjectId _object) const noexcept
{
  const Body* body = FindBody(_object);
  return body != nullptr ? &m_content->archetypes[body->archetype] : nullptr;
}

std::vector<ShipModuleState> Combat::Modules(ObjectId _object) const
{
  std::vector<ShipModuleState> modules;
  const Body* body = FindBody(_object);
  const HullRecord* hull = body != nullptr ? HullOf(*body) : nullptr;
  if (hull == nullptr)
  {
    return modules;
  }
  const Neuron::VoxelMask& full = m_content->archetypes[body->archetype].full;
  for (const ModuleRegion& region : hull->regions)
  {
    std::uint32_t total = 0;
    std::uint32_t present = 0;
    for (int z = region.min.z; z < region.max.z; ++z)
    {
      for (int y = region.min.y; y < region.max.y; ++y)
      {
        for (int x = region.min.x; x < region.max.x; ++x)
        {
          if (full.Present(x, y, z))
          {
            ++total;
            present += body->mask.Present(x, y, z) ? 1u : 0u;
          }
        }
      }
    }
    modules.push_back(ShipModuleState{region.name, HealthPercent(present, total)});
  }
  return modules;
}

int Combat::HullPercent(ObjectId _object) const
{
  const Body* body = FindBody(_object);
  if (body == nullptr)
  {
    return 100;
  }
  const Neuron::VoxelMask& structure = m_content->archetypes[body->archetype].structure;
  std::uint32_t total = 0;
  std::uint32_t present = 0;
  for (std::uint32_t i = 0; i < structure.CellCount(); ++i)
  {
    if (structure.PresentAt(i))
    {
      ++total;
      present += body->mask.PresentAt(i) ? 1u : 0u;
    }
  }
  return Outpost::HealthPercent(present, total);
}

int Combat::CoreHealthPercent(ObjectId _object) const
{
  const Body* body = FindBody(_object);
  if (body == nullptr)
  {
    return 100;
  }
  const std::vector<std::uint32_t>& core = m_content->archetypes[body->archetype].coreCells;
  const auto present = std::ranges::count_if(core, [&](std::uint32_t _cell) { return body->mask.PresentAt(_cell); });
  return Outpost::HealthPercent(static_cast<std::uint32_t>(present), static_cast<std::uint32_t>(core.size()));
}

bool Combat::CoreStopped(ObjectId _object) const
{
  const Body* body = FindBody(_object);
  if (body == nullptr || m_content->archetypes[body->archetype].coreCells.empty())
  {
    return false;
  }
  return StatusOf(m_content->hulls.ThresholdFor(ModuleFunction::Core), CoreHealthPercent(_object)) == ModuleStatus::Stopped;
}

bool Combat::SetMask(ObjectId _object, const Neuron::VoxelMask& _mask)
{
  Body* body = FindBody(_object);
  if (body == nullptr || !(body->mask.Size() == _mask.Size()))
  {
    return false;
  }
  const std::uint64_t last = body->mask.LastEvent();
  body->mask = _mask;
  body->mask.SetLastEvent(last);
  return true;
}

void Combat::BreakUp(ObjectId _object, const Pose& _pose, float _scale, const XMFLOAT3& _velocity)
{
  Body* body = FindBody(_object);
  if (body == nullptr)
  {
    return;
  }
  SpawnWrecks(*body, Neuron::DetachedParts(body->mask, {}, {}), _pose, _scale, _velocity);
  Remove(_object);
}

void Combat::Shatter(ObjectId _object, const Pose& _pose, float _scale, const XMFLOAT3& _velocity)
{
  Body* body = FindBody(_object);
  if (body == nullptr)
  {
    return;
  }
  SpawnWrecks(*body, Neuron::DetachedParts(body->mask, {}, {}), _pose, _scale, _velocity);
  const std::uint64_t last = body->mask.LastEvent();
  body->mask = Neuron::VoxelMask(body->mask.Size());
  body->mask.SetLastEvent(last);
}

void Combat::Remove(ObjectId _object)
{
  std::erase_if(m_bodies, [_object](const Body& _body) { return _body.object == _object; });
}

std::size_t Combat::Patch(ObjectId _object, std::uint32_t _maxCells, std::uint64_t _tick)
{
  static_cast<void>(_tick);
  Body* body = FindBody(_object);
  if (body == nullptr)
  {
    return 0;
  }
  MaskEvent event;
  event.target = _object;
  event.kind = MaskEventKind::Patch;
  event.patch = Neuron::PatchSpec{body->nextEvent++, _maxCells};
  event.cause = DamageCause::Repair;
  event.inflictor = _object;
  const MaskEventResult result = ApplyMaskEvent(m_content->archetypes[body->archetype], event, body->mask);
  Log(event);
  return result.restored.size();
}

void Combat::Log(const MaskEvent& _event)
{
  if (m_events.size() >= MAX_LOGGED_EVENTS)
  {
    m_events.erase(m_events.begin());
  }
  m_events.push_back(_event);
}

void Combat::SpawnWrecks(const Body& _body, const std::vector<std::vector<std::uint32_t>>& _parts, const Pose& _pose, float _scale,
                         const XMFLOAT3& _velocity)
{
  for (const std::vector<std::uint32_t>& part : _parts)
  {
    Wreck wreck;
    wreck.parent = _body.object;
    wreck.archetype = _body.archetype;
    wreck.mask = Neuron::VoxelMask(_body.mask.Size());
    XMFLOAT3 centroid{};
    for (const std::uint32_t cell : part)
    {
      wreck.mask.SetAt(cell, true);
      const Neuron::Int3 at = _body.mask.CellOf(cell);
      centroid.x += static_cast<float>(at.x) + 0.5f;
      centroid.y += static_cast<float>(at.y) + 0.5f;
      centroid.z += static_cast<float>(at.z) + 0.5f;
    }
    const float count = static_cast<float>(std::max<std::size_t>(part.size(), 1));
    centroid = XMFLOAT3{centroid.x / count, centroid.y / count, centroid.z / count};
    const XMFLOAT3 model = Neuron::VoxPointToModel(centroid, _body.pivotVox, _body.voxelSize);
    const XMVECTOR away = XMVector3Rotate(XMVectorScale(XMLoadFloat3(&model), _scale), XMLoadFloat4(&_pose.orientation));
    const float distance = XMVectorGetX(XMVector3Length(away));
    const XMVECTOR kick = distance > 1.0e-3f ? XMVectorScale(away, WRECK_KICK_METRES_PER_SECOND / distance) : XMVectorZero();
    XMStoreFloat3(&wreck.velocityMetresPerSecond, XMVectorAdd(XMLoadFloat3(&_velocity), kick));
    wreck.pose = _pose;
    wreck.scale = _scale;
    m_wrecks.push_back(std::move(wreck));
  }
}

} // namespace Outpost
