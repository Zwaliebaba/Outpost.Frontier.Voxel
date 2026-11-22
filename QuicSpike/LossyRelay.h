#pragma once

#include "WindowsSdk.h"

#include <winsock2.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace Outpost
{

/// A UDP relay on 127.0.0.1 that drops a share of the packets it forwards, in both directions, so
/// that QUIC meets real loss: stream data has to be retransmitted and datagrams are simply gone.
/// The drops come from a seeded generator, so a run can be repeated.
class LossyRelay
{
public:
  LossyRelay() = default;
  ~LossyRelay();
  LossyRelay(const LossyRelay&) = delete;
  LossyRelay& operator=(const LossyRelay&) = delete;

  /// Forwards what arrives on _listenPort to _serverPort and the replies back to the sender.
  [[nodiscard]] bool Start(std::uint16_t _listenPort, std::uint16_t _serverPort, int _dropPercent, std::uint64_t _seed,
                           std::string& _outError);
  void Stop();

  [[nodiscard]] std::uint64_t Forwarded() const noexcept
  {
    return m_forwarded.load();
  }
  [[nodiscard]] std::uint64_t Dropped() const noexcept
  {
    return m_dropped.load();
  }

private:
  void Run(int _dropPercent, std::uint64_t _seed);

  SOCKET m_clientSide = INVALID_SOCKET; // where the client sends
  SOCKET m_serverSide = INVALID_SOCKET; // connected to the server
  std::thread m_thread;
  std::atomic<bool> m_stop{false};
  std::atomic<std::uint64_t> m_forwarded{0};
  std::atomic<std::uint64_t> m_dropped{0};
};

} // namespace Outpost
