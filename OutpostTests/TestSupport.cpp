#include "TestSupport.h"

#include "FileSys.h"
#include "VoxFile.h"

#include <cmath>
#include <stdexcept>

namespace OutpostTests
{

namespace
{

int g_moduleAnchor = 0;

} // namespace

std::filesystem::path AssetRoot()
{
  const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
  if (!root)
  {
    throw std::runtime_error("Assets/Scene.txt not found above the test DLL");
  }
  return *root;
}

Outpost::SceneDefinition LoadRealScene()
{
  const std::filesystem::path root = AssetRoot();
  Outpost::SceneDefinition scene;
  std::string error;
  if (!Outpost::LoadScene(root / "Scene.txt", scene, error))
  {
    throw std::runtime_error(error);
  }
  for (Outpost::ModelAsset& model : scene.models)
  {
    Neuron::VoxDocument document;
    if (Neuron::LoadVoxFile(root / model.file, document) != Neuron::VoxError::None)
    {
      throw std::runtime_error("cannot load " + model.file);
    }
    model.SetSize(Neuron::Int3{static_cast<int>(document.model.SizeX()), static_cast<int>(document.model.SizeY()),
                               static_cast<int>(document.model.SizeZ())});
  }
  return scene;
}

bool Near(float _a, float _b, float _tolerance) noexcept
{
  return std::abs(_a - _b) <= _tolerance;
}

bool Near(const DirectX::XMFLOAT3& _a, const DirectX::XMFLOAT3& _b, float _tolerance) noexcept
{
  return Near(_a.x, _b.x, _tolerance) && Near(_a.y, _b.y, _tolerance) && Near(_a.z, _b.z, _tolerance);
}

DirectX::XMFLOAT3 Row(DirectX::FXMMATRIX _matrix, int _row) noexcept
{
  DirectX::XMFLOAT3 row{};
  DirectX::XMStoreFloat3(&row, _matrix.r[_row]);
  return row;
}

void ByteWriter::Tag(const char (&_tag)[5])
{
  for (int i = 0; i < 4; ++i)
  {
    m_bytes.push_back(static_cast<std::uint8_t>(_tag[i]));
  }
}

void ByteWriter::Int(std::int32_t _value)
{
  const auto value = static_cast<std::uint32_t>(_value);
  for (int shift = 0; shift < 32; shift += 8)
  {
    m_bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

} // namespace OutpostTests
