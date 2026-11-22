#include "QuicLibrary.h"

namespace Outpost
{

QuicLibrary::~QuicLibrary()
{
  if (m_api != nullptr && m_close != nullptr)
  {
    m_close(m_api);
  }
  if (m_module != nullptr)
  {
    FreeLibrary(m_module);
  }
}

bool QuicLibrary::Load(std::string& _outError)
{
  // Only the executable's folder: never the working directory or PATH.
  m_module = LoadLibraryExW(L"msquic.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
  if (m_module == nullptr)
  {
    _outError = "msquic.dll was not found beside the executable";
    return false;
  }
  const auto open = reinterpret_cast<decltype(&MsQuicOpenVersion)>(GetProcAddress(m_module, "MsQuicOpenVersion"));
  m_close = reinterpret_cast<decltype(&MsQuicClose)>(GetProcAddress(m_module, "MsQuicClose"));
  if (open == nullptr || m_close == nullptr)
  {
    _outError = "msquic.dll does not export MsQuicOpenVersion and MsQuicClose";
    return false;
  }
  const void* api = nullptr;
  const QUIC_STATUS status = open(QUIC_API_VERSION_2, &api);
  if (QUIC_FAILED(status))
  {
    _outError = "MsQuicOpenVersion failed with status " + std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  m_api = static_cast<const QUIC_API_TABLE*>(api);
  return true;
}

} // namespace Outpost
