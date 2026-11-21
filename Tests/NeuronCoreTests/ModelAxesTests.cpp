#include "TestSupport.h"

#include "ModelAxes.h"

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace NeuronCoreTests
{

TEST_CLASS(ModelAxesTests)
{
public:
  TEST_METHOD(VoxAxesMapToWorldAxes)
  {
    // VOX +X right, +Y forward, +Z up  ->  world +X right, +Z forward, +Y up.
    Assert::IsTrue(Near(Neuron::VoxDirectionToModel(XMFLOAT3{1, 0, 0}), XMFLOAT3{1, 0, 0}));
    Assert::IsTrue(Near(Neuron::VoxDirectionToModel(XMFLOAT3{0, 1, 0}), XMFLOAT3{0, 0, 1}));
    Assert::IsTrue(Near(Neuron::VoxDirectionToModel(XMFLOAT3{0, 0, 1}), XMFLOAT3{0, 1, 0}));
  }

  TEST_METHOD(PointConversionAppliesPivotThenScale)
  {
    const XMFLOAT3 pivot{4.0f, 6.0f, 2.0f};
    const XMFLOAT3 model = Neuron::VoxPointToModel(XMFLOAT3{5.0f, 9.0f, 3.0f}, pivot, 2.0f);
    Assert::IsTrue(Near(model, XMFLOAT3{2.0f, 2.0f, 6.0f}), L"(dx, dz, dy) * voxelSize");
  }

  TEST_METHOD(MatrixAgreesWithThePointFunction)
  {
    const XMFLOAT3 pivot{24.0f, 75.0f, 20.0f};
    const XMMATRIX matrix = Neuron::VoxToModelMatrix(pivot, 1.25f);
    for (const XMFLOAT3& point : {XMFLOAT3{0, 0, 0}, XMFLOAT3{48, 150, 40}, XMFLOAT3{12.5f, 1.0f, 12.0f}})
    {
      XMFLOAT3 viaMatrix{};
      XMStoreFloat3(&viaMatrix, XMVector3TransformCoord(XMLoadFloat3(&point), matrix));
      Assert::IsTrue(Near(viaMatrix, Neuron::VoxPointToModel(point, pivot, 1.25f), 1e-3f));
    }
  }

  TEST_METHOD(ConversionIsAReflectionSoHandednessFlips)
  {
    // A right-handed VOX frame becomes a left-handed world frame: the determinant is negative,
    // and VOX x cross y (= +Z up) maps to world x cross z, which is -Y in plain vector algebra.
    const XMMATRIX matrix = Neuron::VoxToModelMatrix(XMFLOAT3{0, 0, 0}, 1.0f);
    Assert::IsTrue(XMVectorGetX(XMMatrixDeterminant(matrix)) < 0.0f);
  }

  TEST_METHOD(InstanceYawTurnsForwardTowardPositiveX)
  {
    const XMMATRIX instance = Neuron::InstanceMatrix(XMFLOAT3{0, 0, 0}, XMFLOAT3{90.0f, 0.0f, 0.0f}, 1.0f);
    XMFLOAT3 forward{};
    XMStoreFloat3(&forward, XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), instance));
    Assert::IsTrue(Near(forward, XMFLOAT3{1, 0, 0}, 1e-5f));
  }

  TEST_METHOD(MatedSocketsCoincideFaceEachOtherAndShareUp)
  {
    const Neuron::Socket parentSocket{"Dock", XMFLOAT3{64, 30, 17}, XMFLOAT3{1, 0, 0}, XMFLOAT3{0, 0, 1}};
    const Neuron::Socket childSocket{"Mount", XMFLOAT3{0, 18, 15}, XMFLOAT3{-1, 0, 0}, XMFLOAT3{0, 0, 1}};
    const XMFLOAT3 parentPivot{32, 26, 19};
    const XMFLOAT3 childPivot{24, 18, 15};
    const XMMATRIX parentToWorld = Neuron::InstanceMatrix(XMFLOAT3{10, 40, 120}, XMFLOAT3{30, 0, 0}, 1.0f);
    const XMMATRIX parentSocketWorld = Neuron::SocketFrame(parentSocket, parentPivot, 1.0f) * parentToWorld;
    const XMMATRIX childSocketModel = Neuron::SocketFrame(childSocket, childPivot, 1.0f);
    const XMMATRIX childToWorld = Neuron::MateSockets(childSocketModel, parentSocketWorld);
    const XMMATRIX childSocketWorld = childSocketModel * childToWorld;

    Assert::IsTrue(Near(Row(childSocketWorld, 3), Row(parentSocketWorld, 3), 1e-3f), L"origins coincide");
    const XMFLOAT3 parentNormal = Row(parentSocketWorld, 2);
    Assert::IsTrue(Near(Row(childSocketWorld, 2), XMFLOAT3{-parentNormal.x, -parentNormal.y, -parentNormal.z}, 1e-4f),
                   L"outward normals point at each other");
    Assert::IsTrue(Near(Row(childSocketWorld, 1), Row(parentSocketWorld, 1), 1e-4f), L"up vectors agree");
  }
};

} // namespace NeuronCoreTests
