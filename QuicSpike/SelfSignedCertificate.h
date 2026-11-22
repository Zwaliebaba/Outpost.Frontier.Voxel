#pragma once

#include "WindowsSdk.h"

#include <ncrypt.h>
#include <wincrypt.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Outpost
{

/// A self-signed server certificate made for this run, with its private key in a CNG key that is
/// deleted again when the certificate is. A host in the game will make one once and keep it, so
/// that a client which pinned it on first connection recognizes the host afterwards (ADR-017).
class SelfSignedCertificate
{
public:
  SelfSignedCertificate() = default;
  ~SelfSignedCertificate();
  SelfSignedCertificate(const SelfSignedCertificate&) = delete;
  SelfSignedCertificate& operator=(const SelfSignedCertificate&) = delete;

  [[nodiscard]] bool Create(const std::wstring& _keyName, std::string& _outError);

  [[nodiscard]] PCCERT_CONTEXT Context() const noexcept
  {
    return m_context;
  }

  /// SHA-256 of the encoded certificate: what a client pins.
  [[nodiscard]] static std::vector<std::uint8_t> Fingerprint(PCCERT_CONTEXT _certificate);

private:
  NCRYPT_PROV_HANDLE m_provider = 0;
  NCRYPT_KEY_HANDLE m_key = 0;
  PCCERT_CONTEXT m_context = nullptr;
};

} // namespace Outpost
