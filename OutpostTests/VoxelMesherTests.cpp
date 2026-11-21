#include "TestSupport.h"

#include "VoxelMesher.h"
#include "VoxelWireframe.h"

#include "CppUnitTest.h"

#include <algorithm>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace OutpostTests
{

namespace
{

[[nodiscard]] Neuron::Int3 Cross(const Neuron::Int3& _a, const Neuron::Int3& _b) noexcept
{
  return Neuron::Int3{_a.y * _b.z - _a.z * _b.y, _a.z * _b.x - _a.x * _b.z, _a.x * _b.y - _a.y * _b.x};
}

[[nodiscard]] Neuron::Int3 Minus(const Neuron::Int3& _a, const Neuron::Int3& _b) noexcept
{
  return Neuron::Int3{_a.x - _b.x, _a.y - _b.y, _a.z - _b.z};
}

} // namespace

TEST_CLASS(VoxelMesherTests)
{
public:
  TEST_METHOD(SingleVoxelHasSixOutwardFaces)
  {
    Neuron::VoxModel model(3, 3, 3);
    model.Set(1, 1, 1, 9);
    const std::vector<Neuron::VoxelFace> faces = Neuron::MeshFaces(model);
    Assert::AreEqual(std::size_t{6}, faces.size());
    unsigned seen = 0;
    for (const Neuron::VoxelFace& face : faces)
    {
      seen |= 1u << static_cast<unsigned>(face.direction);
      Assert::AreEqual(9, static_cast<int>(face.colorIndex));
      Assert::IsTrue(face.voxel == (Neuron::Int3{1, 1, 1}));
      const Neuron::Int3 normal = Neuron::FaceNormal(face.direction);
      // Winding: counter-clockwise seen from outside, so the corner cross product is the normal.
      const Neuron::Int3 winding = Cross(Minus(face.corners[1], face.corners[0]), Minus(face.corners[2], face.corners[0]));
      Assert::IsTrue(winding == normal, L"corner winding points along the outward normal");
      // Every corner lies on the face's plane, on the voxel's boundary.
      for (const Neuron::Int3& corner : face.corners)
      {
        const int along = corner.x * normal.x + corner.y * normal.y + corner.z * normal.z;
        const int expected = normal.x + normal.y + normal.z > 0 ? 2 : -1; // the voxel spans [1, 2] on every axis
        Assert::AreEqual(expected, along);
        Assert::IsTrue(corner.x >= 1 && corner.x <= 2 && corner.y >= 1 && corner.y <= 2 && corner.z >= 1 && corner.z <= 2);
      }
    }
    Assert::AreEqual(0x3fu, seen, L"one face in each of the six directions");
  }

  TEST_METHOD(AdjacentVoxelsOmitTheSharedFace)
  {
    Neuron::VoxModel model(4, 2, 2);
    model.Set(1, 0, 0, 3);
    model.Set(2, 0, 0, 4);
    const std::vector<Neuron::VoxelFace> faces = Neuron::MeshFaces(model);
    Assert::AreEqual(std::size_t{10}, faces.size());
    for (const Neuron::VoxelFace& face : faces)
    {
      const bool sharedFromLeft = face.voxel == Neuron::Int3{1, 0, 0} && face.direction == Neuron::FaceDirection::PosX;
      const bool sharedFromRight = face.voxel == Neuron::Int3{2, 0, 0} && face.direction == Neuron::FaceDirection::NegX;
      Assert::IsFalse(sharedFromLeft || sharedFromRight, L"the face between two solid voxels is never emitted");
    }
    const auto leftOuter =
      std::ranges::count_if(faces, [](const Neuron::VoxelFace& _face)
                            { return _face.voxel == Neuron::Int3{1, 0, 0} && _face.direction == Neuron::FaceDirection::NegX; });
    Assert::AreEqual(std::ptrdiff_t{1}, static_cast<std::ptrdiff_t>(leftOuter));
  }

  TEST_METHOD(GridEdgesAreExposed)
  {
    Neuron::VoxModel model(1, 1, 1);
    model.Set(0, 0, 0, 1);
    Assert::AreEqual(std::size_t{6}, Neuron::MeshFaces(model).size(), L"faces on the grid boundary are exposed");
  }

  TEST_METHOD(SurfaceDropsEnclosedVoxels)
  {
    Neuron::VoxModel model(3, 3, 3);
    for (int z = 0; z < 3; ++z)
    {
      for (int y = 0; y < 3; ++y)
      {
        for (int x = 0; x < 3; ++x)
        {
          model.Set(x, y, z, 5);
        }
      }
    }
    const Neuron::VoxelSurface surface = Neuron::ExtractSurface(model);
    Assert::AreEqual(std::size_t{27}, surface.solidCount);
    Assert::AreEqual(std::size_t{26}, surface.voxels.size(), L"the centre voxel has no exposed face");
    Assert::AreEqual(std::size_t{54}, surface.faces.size(), L"9 faces on each of 6 sides");
    Assert::IsTrue(surface.boundsMin == (Neuron::Int3{0, 0, 0}));
    Assert::IsTrue(surface.boundsMax == (Neuron::Int3{2, 2, 2}));
    for (const Neuron::SurfaceVoxel& voxel : surface.voxels)
    {
      Assert::IsTrue(voxel.exposedFaceMask != 0);
    }
  }

  TEST_METHOD(SolidCubeWireframeIsTwelveEdges)
  {
    Neuron::VoxModel model(4, 4, 4);
    for (int z = 0; z < 4; ++z)
    {
      for (int y = 0; y < 4; ++y)
      {
        for (int x = 0; x < 4; ++x)
        {
          model.Set(x, y, z, 2);
        }
      }
    }
    Assert::AreEqual(std::size_t{12}, Neuron::BuildVoxelWireframe(Neuron::ExtractSurface(model), 0).size(), L"creases only, merged");
    // A grid line every 2 voxels adds one line across each face in each in-plane direction.
    Assert::AreEqual(std::size_t{12 + 6 * 2}, Neuron::BuildVoxelWireframe(Neuron::ExtractSurface(model), 2).size());
  }

  TEST_METHOD(CoarseWireframeKeepsTheFormAndDropsGreebles)
  {
    Neuron::VoxModel model(8, 8, 8);
    for (int z = 0; z < 8; ++z)
    {
      for (int y = 0; y < 8; ++y)
      {
        for (int x = 0; x < 8; ++x)
        {
          model.Set(x, y, z, 2);
        }
      }
    }
    model.Set(3, 3, 7, 0); // a one-voxel dent the coarse frame should ignore
    const std::vector<Neuron::LatticeSegment> coarse = Neuron::BuildCoarseWireframe(model, 4, 30, 0);
    Assert::AreEqual(std::size_t{12}, coarse.size());
    for (const Neuron::LatticeSegment& segment : coarse)
    {
      for (const Neuron::Int3& p : {segment.a, segment.b})
      {
        Assert::IsTrue((p.x == 0 || p.x == 8) || (p.y == 0 || p.y == 8) || (p.z == 0 || p.z == 8), L"edges lie on the original bounds");
        Assert::IsTrue(p.x >= 0 && p.x <= 8 && p.y >= 0 && p.y <= 8 && p.z >= 0 && p.z <= 8);
      }
    }
  }

  TEST_METHOD(SideProfileOutlinesTheProjection)
  {
    // An L in the y-z plane, two voxels deep in x, with a step in depth on its foot.
    Neuron::VoxModel model(2, 3, 2);
    model.Set(0, 0, 0, 1);
    model.Set(1, 0, 0, 1);
    model.Set(0, 1, 0, 1);
    model.Set(0, 2, 0, 1);
    model.Set(0, 0, 1, 1);
    model.Set(1, 0, 1, 1);
    const Neuron::SideProfile profile = Neuron::BuildSideProfile(Neuron::ExtractSurface(model), 1);
    Assert::AreEqual(std::size_t{10}, profile.outline.size(), L"an L of four cells has a perimeter of 10 unit sides");
    Assert::IsFalse(profile.detail.empty(), L"the depth step between x=1 and x=0 is drawn");
    for (const Neuron::LatticeSegment& segment : profile.outline)
    {
      Assert::AreEqual(segment.a.x, segment.b.x, L"segments lie in one x plane");
    }
  }
};

} // namespace OutpostTests
