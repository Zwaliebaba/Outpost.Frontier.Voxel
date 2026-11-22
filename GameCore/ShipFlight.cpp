#include "ShipFlight.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Outpost
{

ShipBody ShipBodyFor(const SceneDefinition& _scene, const ShipContent& _content, const ShipArchetype& _archetype,
                     const UniverseObject& _object, CollisionBox* _outLocalBox)
{
  ShipBody body;
  body.object = _object.id;
  body.spec = FlightSpecFor(_content, _archetype);
  UniverseObject atOrigin = _object;
  atOrigin.pose = Pose{};
  CollisionBox local = BoxForObject(_scene, atOrigin);
  local.massKilograms = _archetype.massKilograms;
  const double offset = std::sqrt(local.center.x * local.center.x + local.center.y * local.center.y + local.center.z * local.center.z);
  body.radiusMetres = static_cast<float>(offset) + BoundingRadius(local);
  if (_outLocalBox != nullptr)
  {
    *_outLocalBox = local;
  }
  return body;
}

ShipTickResult StepShip(const ShipBody& _ship, const ShipCommand& _command, std::span<const CollisionBox> _bodies,
                        std::uint32_t _ticksPerSecond, FlightState& _state) noexcept
{
  ShipTickResult result;

  // The way ahead, along the nose, as wide as the ship. Only immovable bodies obstruct, so that the
  // client's prediction, which knows no other ship's future, refuses cruise exactly when the server does.
  XMFLOAT3 ahead{};
  XMStoreFloat3(&ahead, XMVectorScale(NoseDirection(_state), CRUISE_CLEARANCE_METRES));
  for (const CollisionBox& body : _bodies)
  {
    SweepHit obstruction;
    if (body.massKilograms <= 0.0f && body.object != _ship.object &&
        SweepSphere(_state.position, ahead, _ship.radiusMetres, body, obstruction))
    {
      result.obstructed = true;
      break;
    }
  }

  const ManoeuvreOutput output = RunManoeuvre(_command.manoeuvre, _command.wishes, _ship.spec, _state, result.obstructed);
  result.controls = output.controls;
  result.arrived = output.arrived;

  const UniversePosition start = _state.position;
  StepFlight(_ship.spec, output.controls, _ticksPerSecond, _state);
  const XMFLOAT3 motion{static_cast<float>(_state.position.x - start.x), static_cast<float>(_state.position.y - start.y),
                        static_cast<float>(_state.position.z - start.z)};

  SweepHit hit;
  if (!SweepSphere(start, motion, _ship.radiusMetres, _bodies, _ship.object, hit))
  {
    return result;
  }
  const float length = XMVectorGetX(XMVector3Length(XMLoadFloat3(&motion)));
  const float travelled = length > 0.0f ? std::max(0.0f, hit.fraction - CONTACT_STANDOFF_METRES / length) : 0.0f;
  _state.position =
    UniversePosition{start.x + static_cast<double>(motion.x * travelled), start.y + static_cast<double>(motion.y * travelled),
                     start.z + static_cast<double>(motion.z * travelled)};

  // The bounce is taken in the body's frame of motion, so a moving body carries the ship with it.
  const XMVECTOR bodyVelocity = XMLoadFloat3(&hit.body->velocityMetresPerSecond);
  XMFLOAT3 relative{};
  XMStoreFloat3(&relative, XMVectorSubtract(XMLoadFloat3(&_state.velocityMetresPerSecond), bodyVelocity));
  result.contact = true;
  result.contactObject = hit.body->object;
  result.contactMoves = hit.body->massKilograms > 0.0f;
  result.contactSpeedMetresPerSecond = XMVectorGetX(XMVector3Length(XMLoadFloat3(&relative)));
  Bounce(relative, hit.normal);
  XMStoreFloat3(&_state.velocityMetresPerSecond, XMVectorAdd(XMLoadFloat3(&relative), bodyVelocity));
  return result;
}

} // namespace Outpost
