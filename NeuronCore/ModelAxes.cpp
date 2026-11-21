#include "ModelAxes.h"

using namespace DirectX;

namespace Neuron
{

XMFLOAT3 VoxDirectionToModel(const XMFLOAT3& _voxDirection) noexcept
{
  return XMFLOAT3{_voxDirection.x, _voxDirection.z, _voxDirection.y};
}

XMFLOAT3 VoxPointToModel(const XMFLOAT3& _voxPoint, const XMFLOAT3& _pivotVox, float _voxelSize) noexcept
{
  const XMFLOAT3 relative{_voxPoint.x - _pivotVox.x, _voxPoint.y - _pivotVox.y, _voxPoint.z - _pivotVox.z};
  const XMFLOAT3 swizzled = VoxDirectionToModel(relative);
  return XMFLOAT3{swizzled.x * _voxelSize, swizzled.y * _voxelSize, swizzled.z * _voxelSize};
}

XMMATRIX VoxToModelMatrix(const XMFLOAT3& _pivotVox, float _voxelSize) noexcept
{
  // Rows say where each VOX axis lands: x -> x, y -> z, z -> y.
  const XMMATRIX swizzle(1.0f, 0.0f, 0.0f, 0.0f, //
                         0.0f, 0.0f, 1.0f, 0.0f, //
                         0.0f, 1.0f, 0.0f, 0.0f, //
                         0.0f, 0.0f, 0.0f, 1.0f);
  return XMMatrixTranslation(-_pivotVox.x, -_pivotVox.y, -_pivotVox.z) * swizzle * XMMatrixScaling(_voxelSize, _voxelSize, _voxelSize);
}

XMMATRIX InstanceMatrix(const XMFLOAT3& _position, const XMFLOAT3& _yawPitchRollDegrees, float _scale) noexcept
{
  const float yaw = XMConvertToRadians(_yawPitchRollDegrees.x);
  const float pitch = XMConvertToRadians(_yawPitchRollDegrees.y);
  const float roll = XMConvertToRadians(_yawPitchRollDegrees.z);
  return XMMatrixScaling(_scale, _scale, _scale) * XMMatrixRotationRollPitchYaw(pitch, yaw, roll) *
         XMMatrixTranslation(_position.x, _position.y, _position.z);
}

XMMATRIX SocketFrame(const Socket& _socket, const XMFLOAT3& _pivotVox, float _voxelSize) noexcept
{
  const XMFLOAT3 originModel = VoxPointToModel(_socket.positionVox, _pivotVox, _voxelSize);
  const XMFLOAT3 normalModel = VoxDirectionToModel(_socket.normalVox);
  const XMFLOAT3 upModel = VoxDirectionToModel(_socket.upVox);
  const XMVECTOR normal = XMVector3Normalize(XMLoadFloat3(&normalModel));
  XMVECTOR up = XMLoadFloat3(&upModel);
  up = XMVector3Normalize(XMVectorSubtract(up, XMVectorMultiply(XMVector3Dot(up, normal), normal)));
  const XMVECTOR right = XMVector3Cross(up, normal);

  XMMATRIX frame;
  frame.r[0] = XMVectorSetW(right, 0.0f);
  frame.r[1] = XMVectorSetW(up, 0.0f);
  frame.r[2] = XMVectorSetW(normal, 0.0f);
  frame.r[3] = XMVectorSet(originModel.x, originModel.y, originModel.z, 1.0f);
  return frame;
}

XMMATRIX MateSockets(FXMMATRIX _childSocketModel, CXMMATRIX _parentSocketWorld) noexcept
{
  // Half a turn about the socket's up axis turns the child's outward normal to face the parent's.
  const XMMATRIX halfTurn = XMMatrixRotationY(XM_PI);
  XMVECTOR determinant;
  const XMMATRIX childSocketInverse = XMMatrixInverse(&determinant, _childSocketModel);
  return childSocketInverse * halfTurn * _parentSocketWorld;
}

} // namespace Neuron
