#include "MainWindow.h"

#include "GraphicsDevice.h"
#include "TacticalClient.h"

#include <windowsx.h>

#include <algorithm>

namespace Outpost
{

namespace
{

constexpr wchar_t WINDOW_CLASS[] = L"OutpostFrontierWindow";
constexpr wchar_t WINDOW_TITLE[] = L"Outpost: Frontier";
constexpr int MIN_CLIENT_WIDTH = 960;
constexpr int MIN_CLIENT_HEIGHT = 540;
constexpr DWORD WINDOWED_STYLE = WS_OVERLAPPEDWINDOW;
constexpr DWORD BORDERLESS_STYLE = WS_POPUP;

[[nodiscard]] DWORD StyleFor(WindowMode _mode) noexcept
{
  return _mode == WindowMode::BorderlessFullscreen ? BORDERLESS_STYLE : WINDOWED_STYLE;
}

[[nodiscard]] MONITORINFO MonitorInfo(HMONITOR _monitor) noexcept
{
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(_monitor, &info);
  return info;
}

[[nodiscard]] float PointerX(LPARAM _lParam) noexcept
{
  return static_cast<float>(GET_X_LPARAM(_lParam));
}

[[nodiscard]] float PointerY(LPARAM _lParam) noexcept
{
  return static_cast<float>(GET_Y_LPARAM(_lParam));
}

[[nodiscard]] std::uint64_t MessageTime() noexcept
{
  return static_cast<std::uint64_t>(static_cast<DWORD>(GetMessageTime()));
}

} // namespace

MainWindow::~MainWindow()
{
  if (m_window != nullptr)
  {
    SetWindowLongPtrW(m_window, GWLP_USERDATA, 0);
    DestroyWindow(m_window);
  }
}

bool MainWindow::Create(HINSTANCE _instance, std::uint32_t _clientWidth, std::uint32_t _clientHeight, WindowMode _mode)
{
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = &MainWindow::WindowProc;
  windowClass.hInstance = _instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  windowClass.lpszClassName = WINDOW_CLASS;
  if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
  {
    return false;
  }
  // The windowed client area is exactly the requested size at the system DPI, centred in the
  // primary monitor's work area. Borderless fullscreen starts on that monitor and remembers this
  // rectangle for the first switch back to a window.
  const MONITORINFO monitor = MonitorInfo(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
  RECT windowed{0, 0, static_cast<LONG>(_clientWidth), static_cast<LONG>(_clientHeight)};
  AdjustWindowRectExForDpi(&windowed, WINDOWED_STYLE, FALSE, 0, GetDpiForSystem());
  const LONG windowedWidth = windowed.right - windowed.left;
  const LONG windowedHeight = windowed.bottom - windowed.top;
  const LONG left = monitor.rcWork.left + std::max(0L, (monitor.rcWork.right - monitor.rcWork.left - windowedWidth) / 2);
  const LONG top = monitor.rcWork.top + std::max(0L, (monitor.rcWork.bottom - monitor.rcWork.top - windowedHeight) / 2);
  m_initialWindowedRect = RECT{left, top, left + windowedWidth, top + windowedHeight};

  m_mode = _mode;
  const RECT& initial = _mode == WindowMode::BorderlessFullscreen ? monitor.rcMonitor : m_initialWindowedRect;
  m_window = CreateWindowExW(0, WINDOW_CLASS, WINDOW_TITLE, StyleFor(_mode), initial.left, initial.top, initial.right - initial.left,
                             initial.bottom - initial.top, nullptr, nullptr, _instance, this);
  return m_window != nullptr;
}

void MainWindow::Show() noexcept
{
  // SW_SHOWDEFAULT would honour a shortcut's "maximized" or "minimized" setting, which for a
  // borderless window means the work area instead of the monitor.
  ShowWindow(m_window, m_mode == WindowMode::BorderlessFullscreen ? SW_SHOWNORMAL : SW_SHOWDEFAULT);
  UpdateWindow(m_window);
}

void MainWindow::SetMode(WindowMode _mode) noexcept
{
  if (_mode == m_mode || m_window == nullptr)
  {
    return;
  }
  if (_mode == WindowMode::BorderlessFullscreen)
  {
    m_windowedPlacement.length = sizeof(m_windowedPlacement);
    m_hasWindowedPlacement = GetWindowPlacement(m_window, &m_windowedPlacement) != FALSE;
    m_mode = _mode;
    SetWindowLongPtrW(m_window, GWL_STYLE, static_cast<LONG_PTR>(BORDERLESS_STYLE | WS_VISIBLE));
    FitToMonitor();
    return;
  }
  m_mode = _mode;
  SetWindowLongPtrW(m_window, GWL_STYLE, static_cast<LONG_PTR>(WINDOWED_STYLE | WS_VISIBLE));
  if (m_hasWindowedPlacement)
  {
    SetWindowPlacement(m_window, &m_windowedPlacement);
    SetWindowPos(m_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
  else
  {
    const RECT& rect = m_initialWindowedRect;
    SetWindowPos(m_window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
}

void MainWindow::FitToMonitor() noexcept
{
  const MONITORINFO monitor = MonitorInfo(MonitorFromWindow(m_window, MONITOR_DEFAULTTONEAREST));
  const RECT& rect = monitor.rcMonitor;
  SetWindowPos(m_window, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
               SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}

void MainWindow::Attach(Neuron::GraphicsDevice* _device, TacticalClient* _client) noexcept
{
  m_device = _device;
  m_client = _client;
}

bool MainWindow::TakeCaptureRequest() noexcept
{
  const bool requested = m_captureRequested;
  m_captureRequested = false;
  return requested;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam)
{
  if (_message == WM_NCCREATE)
  {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 hands this pointer over as an integer.
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(_lParam);
    auto* self = static_cast<MainWindow*>(create->lpCreateParams);
    self->m_window = _window;
    SetWindowLongPtrW(_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 hands this pointer over as an integer.
  auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(_window, GWLP_USERDATA));
  return self != nullptr ? self->HandleMessage(_message, _wParam, _lParam) : DefWindowProcW(_window, _message, _wParam, _lParam);
}

LRESULT MainWindow::HandleMessage(UINT _message, WPARAM _wParam, LPARAM _lParam)
{
  switch (_message)
  {
  case WM_SIZE:
    m_minimized = _wParam == SIZE_MINIMIZED;
    if (!m_minimized && m_device != nullptr && m_client != nullptr)
    {
      const auto width = static_cast<std::uint32_t>(LOWORD(_lParam));
      const auto height = static_cast<std::uint32_t>(HIWORD(_lParam));
      if (width > 0 && height > 0 && (!m_device->Resize(width, height) || !m_client->Resize(width, height)))
      {
        m_deviceLost = true;
      }
    }
    return 0;
  case WM_GETMINMAXINFO:
  {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 hands this pointer over as an integer.
    auto* info = reinterpret_cast<MINMAXINFO*>(_lParam);
    info->ptMinTrackSize.x = MIN_CLIENT_WIDTH;
    info->ptMinTrackSize.y = MIN_CLIENT_HEIGHT;
    return 0;
  }
  case WM_DPICHANGED:
  {
    if (m_mode == WindowMode::BorderlessFullscreen)
    {
      FitToMonitor();
      return 0;
    }
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 hands this pointer over as an integer.
    const auto* suggested = reinterpret_cast<const RECT*>(_lParam);
    SetWindowPos(m_window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  case WM_DISPLAYCHANGE:
    // A resolution or monitor-layout change: stay exactly on the monitor.
    if (m_mode == WindowMode::BorderlessFullscreen)
    {
      FitToMonitor();
    }
    return DefWindowProcW(m_window, _message, _wParam, _lParam);
  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_MBUTTONDOWN:
    if (m_client != nullptr)
    {
      SetCapture(m_window);
      const PointerButton button =
        _message == WM_LBUTTONDOWN ? PointerButton::Left : (_message == WM_RBUTTONDOWN ? PointerButton::Right : PointerButton::Middle);
      m_client->OnPointerDown(button, PointerX(_lParam), PointerY(_lParam), MessageTime());
    }
    return 0;
  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
    if (m_client != nullptr)
    {
      const PointerButton button =
        _message == WM_LBUTTONUP ? PointerButton::Left : (_message == WM_RBUTTONUP ? PointerButton::Right : PointerButton::Middle);
      m_client->OnPointerUp(button, PointerX(_lParam), PointerY(_lParam), MessageTime());
      if ((_wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0)
      {
        ReleaseCapture();
      }
    }
    return 0;
  case WM_MOUSEMOVE:
    if (m_client != nullptr)
    {
      m_client->OnPointerMove(PointerX(_lParam), PointerY(_lParam));
    }
    return 0;
  case WM_CAPTURECHANGED:
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Win32 hands this pointer over as an integer.
    if (m_client != nullptr && reinterpret_cast<HWND>(_lParam) != m_window)
    {
      m_client->OnPointerCancel();
    }
    return 0;
  case WM_MOUSEWHEEL:
    if (m_client != nullptr)
    {
      m_client->OnWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(_wParam)) / static_cast<float>(WHEEL_DELTA));
    }
    return 0;
  case WM_KEYDOWN:
    if (m_client != nullptr)
    {
      switch (_wParam)
      {
      case 'R':
      case VK_HOME:
        m_client->ResetView();
        break;
      case 'H':
        m_client->ToggleHelp();
        break;
      case VK_F3:
        m_client->ToggleStatus();
        break;
      case VK_F11:
        SetMode(m_mode == WindowMode::BorderlessFullscreen ? WindowMode::Windowed : WindowMode::BorderlessFullscreen);
        break;
      case VK_F12:
        m_captureRequested = true;
        break;
      case VK_ESCAPE:
        PostMessageW(m_window, WM_CLOSE, 0, 0);
        break;
      default:
        break;
      }
    }
    return 0;
  case WM_SYSKEYDOWN:
    // Alt+Enter, bit 29 being the Alt state. DXGI's own Alt+Enter handling is off
    // (DXGI_MWA_NO_ALT_ENTER), so this is the only fullscreen switch there is.
    if (_wParam == VK_RETURN && (_lParam & (1 << 29)) != 0)
    {
      if ((_lParam & (1 << 30)) == 0)
      {
        SetMode(m_mode == WindowMode::BorderlessFullscreen ? WindowMode::Windowed : WindowMode::BorderlessFullscreen);
      }
      return 0;
    }
    return DefWindowProcW(m_window, _message, _wParam, _lParam);
  case WM_SYSCHAR:
    // Swallow the Alt+Enter character, which would otherwise sound the menu-mnemonic beep.
    if (_wParam == VK_RETURN)
    {
      return 0;
    }
    return DefWindowProcW(m_window, _message, _wParam, _lParam);
  case WM_CLOSE:
    // Drain the GPU while the window still exists: the last frame's Present targets this window,
    // and everything torn down after the message loop ends assumes no work is in flight.
    if (m_device != nullptr)
    {
      m_device->WaitIdle();
    }
    DestroyWindow(m_window);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(m_window, _message, _wParam, _lParam);
  }
}

} // namespace Outpost
