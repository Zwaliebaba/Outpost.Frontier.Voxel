#pragma once

#include "VoxModel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Neuron
{

struct Int3
{
  int x;
  int y;
  int z;

  friend bool operator==(const Int3&, const Int3&) = default;
};

/// The six face directions, in VOX coordinates (right-handed, +Z up).
enum class FaceDirection : std::uint8_t
{
  PosX,
  NegX,
  PosY,
  NegY,
  PosZ,
  NegZ
};

inline constexpr std::uint32_t FACE_DIRECTION_COUNT = 6;

[[nodiscard]] Int3 FaceNormal(FaceDirection _direction) noexcept;

/// One exposed unit face of a voxel. Corners are lattice points in VOX coordinates, ordered
/// counter-clockwise when seen from outside the voxel: (c1 - c0) x (c2 - c0) points along the
/// outward normal in VOX's right-handed space.
struct VoxelFace
{
  Int3 voxel;
  FaceDirection direction;
  std::uint8_t colorIndex;
  std::array<Int3, 4> corners;
};

/// A voxel with at least one exposed face, packed for the GPU (ADR-003).
struct SurfaceVoxel
{
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t z;
  std::uint8_t colorIndex;
  std::uint8_t exposedFaceMask; // bit FaceDirection set when that face is exposed
  std::uint8_t reserved;
  std::uint16_t faceOcclusion; // 2 bits per FaceDirection: 0 open .. 3 deeply recessed
};

struct VoxelSurface
{
  std::vector<VoxelFace> faces;
  std::vector<SurfaceVoxel> voxels;
  Int3 boundsMin{0, 0, 0}; // inclusive voxel coordinates of the occupied cells
  Int3 boundsMax{-1, -1, -1};
  std::size_t solidCount = 0;
};

/// Exposed faces only: a face shared by two solid voxels is omitted, because it can never be seen.
[[nodiscard]] std::vector<VoxelFace> MeshFaces(const VoxModel& _model);

/// The surface of a model: its exposed faces, the voxels that own them with per-face ambient
/// occlusion, and the bounds of every solid cell. Fully enclosed voxels are dropped.
/// With a palette, translucent neighbors expose the backing surface; equal glass cells still
/// share a hidden face. Glass remains solid for connectivity and gameplay.
[[nodiscard]] VoxelSurface ExtractSurface(const VoxModel& _model, const VoxPalette* _palette = nullptr);

} // namespace Neuron
