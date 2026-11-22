#include "LossyRelay.h"
#include "Random.h"

#include <ws2tcpip.h>

#include <array>

namespace Outpost
{

namespace
{

sockaddr_in Loopback(std::uint16_t _port)
{
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(_port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return address;
}

} // namespace

LossyRelay::~LossyRelay()
{
  Stop();
}

bool LossyRelay::Start(std::uint16_t _listenPort, std::uint16_t _serverPort, int _dropPercent, std::uint64_t _seed, std::string& _outError)
{
  m_clientSide = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  m_serverSide = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (m_clientSide == INVALID_SOCKET || m_serverSide == INVALID_SOCKET)
  {
    _outError = "cannot open the relay's sockets: " + std::to_string(WSAGetLastError());
    return false;
  }
  const sockaddr_in listen = Loopback(_listenPort);
  const sockaddr_in server = Loopback(_serverPort);
  if (bind(m_clientSide, reinterpret_cast<const sockaddr*>(&listen), sizeof(listen)) != 0 ||
      connect(m_serverSide, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) != 0)
  {
    _outError = "cannot bind the relay to port " + std::to_string(_listenPort) + ": " + std::to_string(WSAGetLastError());
    return false;
  }
  m_stop = false;
  m_thread = std::thread([this, _dropPercent, _seed] { Run(_dropPercent, _seed); });
  return true;
}

void LossyRelay::Stop()
{
  m_stop = true;
  if (m_thread.joinable())
  {
    m_thread.join();
  }
  if (m_clientSide != INVALID_SOCKET)
  {
    closesocket(m_clientSide);
    m_clientSide = INVALID_SOCKET;
  }
  if (m_serverSide != INVALID_SOCKET)
  {
    closesocket(m_serverSide);
    m_serverSide = INVALID_SOCKET;
  }
}

void LossyRelay::Run(int _dropPercent, std::uint64_t _seed)
{
  Neuron::Pcg32 random(_seed);
  sockaddr_in client{};
  bool knowClient = false;
  std::array<char, 65'536> packet{};
  while (!m_stop)
  {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(m_clientSide, &readable);
    FD_SET(m_serverSide, &readable);
    timeval wait{0, 10'000};
    if (select(0, &readable, nullptr, nullptr, &wait) <= 0)
    {
      continue;
    }
    if (FD_ISSET(m_clientSide, &readable))
    {
      int fromBytes = sizeof(client);
      const int bytes =
        recvfrom(m_clientSide, packet.data(), static_cast<int>(packet.size()), 0, reinterpret_cast<sockaddr*>(&client), &fromBytes);
      if (bytes > 0)
      {
        knowClient = true;
        if (random.RangeInt(0, 99) < _dropPercent)
        {
          ++m_dropped;
        }
        else
        {
          send(m_serverSide, packet.data(), bytes, 0);
          ++m_forwarded;
        }
      }
    }
    if (FD_ISSET(m_serverSide, &readable))
    {
      const int bytes = recv(m_serverSide, packet.data(), static_cast<int>(packet.size()), 0);
      if (bytes > 0 && knowClient)
      {
        if (random.RangeInt(0, 99) < _dropPercent)
        {
          ++m_dropped;
        }
        else
        {
          sendto(m_clientSide, packet.data(), bytes, 0, reinterpret_cast<const sockaddr*>(&client), sizeof(client));
          ++m_forwarded;
        }
      }
    }
  }
}

} // namespace Outpost
