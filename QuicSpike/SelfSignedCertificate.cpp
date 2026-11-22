#include "SelfSignedCertificate.h"

#include <bcrypt.h>

namespace Outpost
{

SelfSignedCertificate::~SelfSignedCertificate()
{
  if (m_context != nullptr)
  {
    CertFreeCertificateContext(m_context);
  }
  if (m_key != 0)
  {
    NCryptDeleteKey(m_key, 0); // also frees the handle
  }
  if (m_provider != 0)
  {
    NCryptFreeObject(m_provider);
  }
}

bool SelfSignedCertificate::Create(const std::wstring& _keyName, std::string& _outError)
{
  if (NCryptOpenStorageProvider(&m_provider, MS_KEY_STORAGE_PROVIDER, 0) != ERROR_SUCCESS)
  {
    _outError = "cannot open the software key storage provider";
    return false;
  }
  if (NCryptCreatePersistedKey(m_provider, &m_key, NCRYPT_RSA_ALGORITHM, _keyName.c_str(), 0, NCRYPT_OVERWRITE_KEY_FLAG) != ERROR_SUCCESS)
  {
    _outError = "cannot create the certificate's key";
    return false;
  }
  DWORD bits = 2048;
  if (NCryptSetProperty(m_key, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&bits), sizeof(bits), 0) != ERROR_SUCCESS ||
      NCryptFinalizeKey(m_key, 0) != ERROR_SUCCESS)
  {
    _outError = "cannot finalize the certificate's key";
    return false;
  }

  BYTE subject[256]{};
  DWORD subjectBytes = sizeof(subject);
  if (!CertStrToNameW(X509_ASN_ENCODING, L"CN=localhost", CERT_X500_NAME_STR, nullptr, subject, &subjectBytes, nullptr))
  {
    _outError = "cannot encode the certificate's subject";
    return false;
  }
  CERT_NAME_BLOB subjectBlob{subjectBytes, subject};

  // Server authentication, which Schannel expects of a server certificate.
  char serverAuthentication[] = szOID_PKIX_KP_SERVER_AUTH;
  LPSTR usages[] = {serverAuthentication};
  CERT_ENHKEY_USAGE usage{1, usages};
  BYTE* encodedUsage = nullptr;
  DWORD encodedUsageBytes = 0;
  if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &usage, CRYPT_ENCODE_ALLOC_FLAG, nullptr,
                           static_cast<void*>(&encodedUsage), &encodedUsageBytes))
  {
    _outError = "cannot encode the certificate's key usage";
    return false;
  }
  char usageOid[] = szOID_ENHANCED_KEY_USAGE;
  CERT_EXTENSION extension{usageOid, FALSE, {encodedUsageBytes, encodedUsage}};
  CERT_EXTENSIONS extensions{1, &extension};

  // The structure's fields are not const, so they point at copies rather than at the literal.
  std::wstring container = _keyName;
  std::wstring provider = MS_KEY_STORAGE_PROVIDER;
  CRYPT_KEY_PROV_INFO keyInfo{};
  keyInfo.pwszContainerName = container.data();
  keyInfo.pwszProvName = provider.data();
  char signatureOid[] = szOID_RSA_SHA256RSA;
  CRYPT_ALGORITHM_IDENTIFIER signature{signatureOid, {}};
  m_context = CertCreateSelfSignCertificate(m_key, &subjectBlob, 0, &keyInfo, &signature, nullptr, nullptr, &extensions);
  LocalFree(encodedUsage);
  if (m_context == nullptr)
  {
    _outError = "CertCreateSelfSignCertificate failed with " + std::to_string(GetLastError());
    return false;
  }
  return true;
}

std::vector<std::uint8_t> SelfSignedCertificate::Fingerprint(PCCERT_CONTEXT _certificate)
{
  std::vector<std::uint8_t> hash(32);
  DWORD hashBytes = static_cast<DWORD>(hash.size());
  if (_certificate == nullptr || !CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, _certificate->pbCertEncoded,
                                                        _certificate->cbCertEncoded, hash.data(), &hashBytes))
  {
    return {};
  }
  hash.resize(hashBytes);
  return hash;
}

} // namespace Outpost
