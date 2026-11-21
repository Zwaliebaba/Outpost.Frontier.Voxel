#pragma once

#include "SceneDefinition.h"
#include "VoxModel.h"

#include <DirectXMath.h>

#include <filesystem>

namespace GameCoreTests
{

/// The repository's Assets directory, found by walking up from the test DLL.
[[nodiscard]] std::filesystem::path AssetRoot();

/// Assets/Scene.txt loaded, with every model's size read from its .vox file.
[[nodiscard]] Outpost::SceneDefinition LoadRealScene();

[[nodiscard]] bool Near(float _a, float _b, float _tolerance = 1e-4f) noexcept;
[[nodiscard]] bool Near(const DirectX::XMFLOAT3& _a, const DirectX::XMFLOAT3& _b, float _tolerance = 1e-4f) noexcept;
[[nodiscard]] DirectX::XMFLOAT3 Row(DirectX::FXMMATRIX _matrix, int _row) noexcept;

} // namespace GameCoreTests
