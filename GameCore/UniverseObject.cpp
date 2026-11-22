#include "UniverseObject.h"

using namespace DirectX;

namespace Outpost
{

XMMATRIX ObjectToWorld(const UniverseObject& _object) noexcept
{
  const Pose& pose = _object.pose;
  return XMMatrixScaling(_object.scale, _object.scale, _object.scale) * XMMatrixRotationQuaternion(XMLoadFloat4(&pose.orientation)) *
         XMMatrixTranslation(static_cast<float>(pose.position.x), static_cast<float>(pose.position.y), static_cast<float>(pose.position.z));
}

} // namespace Outpost
