#pragma once

#include "WindowsSdk.h"

#include <cstdint>

namespace Neuron
{
class GraphicsDevice;
}

namespace Outpost
{

class TacticalClient;

/// How the window occupies the desktop. Borderless fullscreen is a popup window covering one
/// monitor: the swap chain stays an ordinary flip-model one and DXGI's exclusive fullscreen is
/// never used (ADR-004).
enum class WindowMode : std::uint8_t
{
  Windowed,
  BorderlessFullscreen
};

/// The Win32 window: creation in either mode, switching between them, DPI and display changes,
/// resizing the swap chain, and mouse and keyboard input forwarded to the client.
class MainWindow
{
public:
  MainWindow() = default;
  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;
  ~MainWindow();

  /// _clientWidth x _clientHeight is the client size in windowed mode; borderless fullscreen
  /// covers the primary monitor and keeps that size for when the window is switched back.
  [[nodiscard]] bool Create(HINSTANCE _instance, std::uint32_t _clientWidth, std::uint32_t _clientHeight, WindowMode _mode);
  void Show() noexcept;

  /// Switches between a framed window and borderless fullscreen on the window's current monitor.
  void SetMode(WindowMode _mode) noexcept;
  [[nodiscard]] WindowMode Mode() const noexcept
  {
    return m_mode;
  }
  void Attach(Neuron::GraphicsDevice* _device, TacticalClient* _client) noexcept;

  [[nodiscard]] HWND Handle() const noexcept
  {
    return m_window;
  }
  [[nodiscard]] bool IsMinimized() const noexcept
  {
    return m_minimized;
  }
  [[nodiscard]] bool DeviceLost() const noexcept
  {
    return m_deviceLost;
  }

  /// True once after F12 was pressed.
  [[nodiscard]] bool TakeCaptureRequest() noexcept;

private:
  static LRESULT CALLBACK WindowProc(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam);
  LRESULT HandleMessage(UINT _message, WPARAM _wParam, LPARAM _lParam);
  void FitToMonitor() noexcept;

  HWND m_window = nullptr;
  WindowMode m_mode = WindowMode::Windowed;
  WINDOWPLACEMENT m_windowedPlacement{};
  bool m_hasWindowedPlacement = false;
  RECT m_initialWindowedRect{};
  Neuron::GraphicsDevice* m_device = nullptr;
  TacticalClient* m_client = nullptr;
  bool m_minimized = false;
  bool m_captureRequested = false;
  bool m_deviceLost = false;
};

} // namespace Outpost
