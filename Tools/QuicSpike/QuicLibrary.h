#pragma once

#include "WindowsSdk.h"

#include <msquic.h>

#include <string>

namespace Outpost
{

/// msquic.dll, loaded at run time from the executable's own folder and never linked (ADR-017), so
/// a process that does not ask for the network never loads it. The game's network transport will
/// load it the same way in slice 10.
class QuicLibrary
{
public:
  QuicLibrary() = default;
  ~QuicLibrary();
  QuicLibrary(const QuicLibrary&) = delete;
  QuicLibrary& operator=(const QuicLibrary&) = delete;

  [[nodiscard]] bool Load(std::string& _outError);

  [[nodiscard]] const QUIC_API_TABLE& Api() const noexcept
  {
    return *m_api;
  }

private:
  HMODULE m_module = nullptr;
  const QUIC_API_TABLE* m_api = nullptr;
  decltype(&MsQuicClose) m_close = nullptr;
};

} // namespace Outpost
