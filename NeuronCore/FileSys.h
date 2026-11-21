#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

[[nodiscard]] bool ReadFileBytes(const std::filesystem::path& _path, std::vector<std::uint8_t>& _outBytes);
[[nodiscard]] bool ReadTextFile(const std::filesystem::path& _path, std::string& _outText);

/// Writes the whole file, creating parent directories. Overwrites an existing file.
[[nodiscard]] bool WriteFileBytes(const std::filesystem::path& _path, std::span<const std::uint8_t> _bytes);

/// The directory of the executable or DLL that contains _addressInModule. Asset paths are resolved
/// from here and never from the process working directory.
[[nodiscard]] std::filesystem::path ModuleDirectory(const void* _addressInModule);

/// Looks for `<dir>/Assets/<_marker>` in _start and up to _maxParents directories above it, and
/// returns the first matching Assets directory. A deployed build finds the Assets folder copied
/// beside the executable; a test DLL in x64/<Config>/ finds the repository's Assets folder.
[[nodiscard]] std::optional<std::filesystem::path> FindAssetRoot(const std::filesystem::path& _start, const std::filesystem::path& _marker,
                                                                 int _maxParents = 4);

} // namespace Neuron
