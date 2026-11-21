#include "FileSys.h"

#include "WindowsSdk.h"

#include <fstream>
#include <system_error>

namespace Neuron
{

bool ReadFileBytes(const std::filesystem::path& _path, std::vector<std::uint8_t>& _outBytes)
{
  _outBytes.clear();
  std::ifstream file(_path, std::ios::binary | std::ios::ate);
  if (!file)
  {
    return false;
  }
  const std::streamoff size = file.tellg();
  if (size < 0)
  {
    return false;
  }
  _outBytes.resize(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (size > 0 && !file.read(reinterpret_cast<char*>(_outBytes.data()), size))
  {
    _outBytes.clear();
    return false;
  }
  return true;
}

bool ReadTextFile(const std::filesystem::path& _path, std::string& _outText)
{
  std::vector<std::uint8_t> bytes;
  if (!ReadFileBytes(_path, bytes))
  {
    return false;
  }
  _outText.assign(bytes.begin(), bytes.end());
  return true;
}

bool WriteFileBytes(const std::filesystem::path& _path, std::span<const std::uint8_t> _bytes)
{
  std::error_code error;
  if (_path.has_parent_path())
  {
    std::filesystem::create_directories(_path.parent_path(), error);
  }
  std::ofstream file(_path, std::ios::binary | std::ios::trunc);
  if (!file)
  {
    return false;
  }
  file.write(reinterpret_cast<const char*>(_bytes.data()), static_cast<std::streamsize>(_bytes.size()));
  return static_cast<bool>(file);
}

std::filesystem::path ModuleDirectory(const void* _addressInModule)
{
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCWSTR>(_addressInModule), &module))
  {
    return {};
  }
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;)
  {
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
    {
      return {};
    }
    if (length < buffer.size())
    {
      buffer.resize(length);
      break;
    }
    buffer.resize(buffer.size() * 2);
  }
  return std::filesystem::path(buffer).parent_path();
}

std::optional<std::filesystem::path> FindAssetRoot(const std::filesystem::path& _start, const std::filesystem::path& _marker,
                                                   int _maxParents)
{
  std::error_code error;
  std::filesystem::path directory = _start;
  for (int level = 0; level <= _maxParents && !directory.empty(); ++level)
  {
    std::filesystem::path candidate = directory / L"Assets";
    if (std::filesystem::exists(candidate / _marker, error))
    {
      return candidate;
    }
    const std::filesystem::path parent = directory.parent_path();
    if (parent == directory)
    {
      break;
    }
    directory = parent;
  }
  return std::nullopt;
}

} // namespace Neuron
