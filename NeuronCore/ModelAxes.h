#pragma once

#include <DirectXMath.h>

#include <string>

namespace Neuron
{

// ── VOX to world (ADR-002) ───────────────────────────────────────────────────────────────────
//
// VOX space (MagicaVoxel): right-handed, +X right, +Y forward, +Z up. A voxel (x, y, z) occupies
// the unit cube [x, x+1] x [y, y+1] x [z, z+1].
//
// World and model space (Direct3D): left-handed, +X right, +Y up, +Z forward.
//
//   model = (vox.x - pivot.x, vox.z - pivot.z, vox.y - pivot.y) * voxelSize
//
// Swapping Y and Z is a reflection, and a reflection is exactly what turns a right-handed frame
// into a left-handed one, so no axis is negated: VOX +Y (forward) is world +Z (forward), and VOX
// +Z (up) is world +Y (up). Every consumer — the GPU voxel buffers, sockets, bounds, picking and
// the blueprint wireframe — goes through the functions below and nothing else.
//
// A ship model faces VOX +Y, which is model +Z: yaw 0 points it into the screen.

/// A direction (no pivot, no scale) from VOX axes to model axes.
[[nodiscard]] DirectX::XMFLOAT3 VoxDirectionToModel(const DirectX::XMFLOAT3& _voxDirection) noexcept;

/// A point in VOX lattice coordinates to model space.
[[nodiscard]] DirectX::XMFLOAT3 VoxPointToModel(const DirectX::XMFLOAT3& _voxPoint, const DirectX::XMFLOAT3& _pivotVox,
                                                float _voxelSize) noexcept;

/// The same conversion as a row-vector matrix: modelPoint = voxPoint * VoxToModelMatrix(...).
[[nodiscard]] DirectX::XMMATRIX VoxToModelMatrix(const DirectX::XMFLOAT3& _pivotVox, float _voxelSize) noexcept;

/// An instance placement: uniform scale, then yaw/pitch/roll in degrees, then translation.
/// Yaw turns +Z toward +X.
[[nodiscard]] DirectX::XMMATRIX InstanceMatrix(const DirectX::XMFLOAT3& _position, const DirectX::XMFLOAT3& _yawPitchRollDegrees,
                                               float _scale) noexcept;

/// An attachment point on a model, authored in VOX coordinates.
struct Socket
{
  std::string name;
  DirectX::XMFLOAT3 positionVox;
  DirectX::XMFLOAT3 normalVox; // outward
  DirectX::XMFLOAT3 upVox;
};

/// The socket's frame in model space as a row-vector matrix: rows are right, up, outward normal
/// and origin. Right is up x normal, which is +X for up +Y and normal +Z in a left-handed frame.
[[nodiscard]] DirectX::XMMATRIX SocketFrame(const Socket& _socket, const DirectX::XMFLOAT3& _pivotVox, float _voxelSize) noexcept;

/// The child-model-to-world matrix that mates a child socket with a parent socket: the socket
/// origins coincide, the outward normals point at each other, and the up vectors agree.
/// _childSocketModel is SocketFrame() of the child; _parentSocketWorld is the parent's
/// SocketFrame() multiplied by the parent's model-to-world matrix.
[[nodiscard]] DirectX::XMMATRIX MateSockets(DirectX::FXMMATRIX _childSocketModel, DirectX::CXMMATRIX _parentSocketWorld) noexcept;

} // namespace Neuron
