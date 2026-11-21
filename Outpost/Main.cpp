// Outpost.exe: the client with the server's logic in-process (ADR-001). The server half — World and
// TickClock — owns the game state; the client half — TacticalClient — draws snapshots of it.
//
// With --smoke-test it initializes, loads the scene, renders a few frames, checks the last one and
// exits with an ExitCode, which is what CI and the README's verification steps run.

#include "CommandLine.h"
#include "MainWindow.h"

#include "FileSys.h"
#include "FrameCapture.h"
#include "GraphicsDevice.h"
#include "TacticalClient.h"
#include "TickClock.h"
#include "World.h"

#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using Outpost::ExitCode;

int g_moduleAnchor = 0;

/// Smoke-test output goes to the parent console when there is one, to the debugger, and to the
/// report file when one was asked for.
class Report
{
public:
  explicit Report(bool _attachConsole)
  {
    if (_attachConsole && AttachConsole(ATTACH_PARENT_PROCESS))
    {
      FILE* stream = nullptr;
      m_console = freopen_s(&stream, "CONOUT$", "w", stdout) == 0;
    }
  }

  void Line(const std::string& _text)
  {
    m_text << _text << '\n';
    OutputDebugStringA(("Outpost: " + _text + "\n").c_str());
    if (m_console)
    {
      std::printf("%s\n", _text.c_str());
      std::fflush(stdout);
    }
  }

  void Save(const std::filesystem::path& _path) const
  {
    if (!_path.empty())
    {
      std::ofstream file(_path, std::ios::binary);
      file << m_text.str();
    }
  }

private:
  std::ostringstream m_text;
  bool m_console = false;
};

[[nodiscard]] std::string Narrow(const std::wstring& _text)
{
  if (_text.empty())
  {
    return {};
  }
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, _text.data(), static_cast<int>(_text.size()), nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<std::size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, _text.data(), static_cast<int>(_text.size()), result.data(), bytes, nullptr, nullptr);
  return result;
}

[[nodiscard]] std::vector<std::wstring> Arguments()
{
  std::vector<std::wstring> arguments;
  int count = 0;
  LPWSTR* list = CommandLineToArgvW(GetCommandLineW(), &count);
  if (list != nullptr)
  {
    for (int i = 1; i < count; ++i)
    {
      arguments.emplace_back(list[i]);
    }
    LocalFree(static_cast<HLOCAL>(list));
  }
  return arguments;
}

[[nodiscard]] Neuron::Image CapturedImage(const Neuron::GraphicsDevice& _device)
{
  return Neuron::Image{_device.CapturedWidth(), _device.CapturedHeight(), _device.CapturedPixels()};
}

[[nodiscard]] std::filesystem::path ScreenshotPath()
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
  return Neuron::ModuleDirectory(&g_moduleAnchor) / "Captures" / ("Outpost-" + std::to_string(stamp) + ".png");
}

struct Session
{
  Outpost::MainWindow window;
  Neuron::GraphicsDevice device;
  Outpost::TacticalClient client;
  Outpost::World world;
};

/// Writes the debug layer's tally and every message it kept to the report; returns the error count.
std::uint32_t ReportDebugLayer(Session& _session, const Outpost::LaunchOptions& _options, Report& _report)
{
  const std::uint32_t debugErrors = _session.device.DebugErrorCount();
  _report.Line("debug layer: " + std::string(_options.debugLayer ? "on" : "off") + ", " + std::to_string(debugErrors) + " error(s), " +
               std::to_string(_session.device.DebugWarningCount()) + " warning(s)");
  for (const std::string& message : _session.device.DebugMessages())
  {
    _report.Line("  " + message);
  }
  return debugErrors;
}

[[nodiscard]] ExitCode RunSmokeTest(Session& _session, const Outpost::LaunchOptions& _options, Report& _report)
{
  // A fixed timeline: frame i is at i/60 s and advances the server by one tick, so the capture is
  // the same on every run and every machine.
  for (int frame = 0; frame < _options.frames; ++frame)
  {
    _session.world.Tick();
    const bool last = frame + 1 == _options.frames;
    const float time = static_cast<float>(frame) / 60.0f;
    if (!_session.client.Render(_session.world.Snapshot(), time, last, false, 60.0f))
    {
      _report.Line("FAIL render: the device was removed during frame " + std::to_string(frame));
      return ExitCode::Render;
    }
  }
  _session.device.WaitIdle();
  _report.Line("rendered " + std::to_string(_options.frames) + " frame(s), server tick " + std::to_string(_session.world.TickCount()));

  const Neuron::Image image = CapturedImage(_session.device);
  const Neuron::ImageStatistics statistics = Neuron::MeasureImage(image);
  char line[256]{};
  std::snprintf(line, sizeof(line), "image %ux%u: mean luminance %.3f, lit %.1f%%, cyan %.2f%%, warm %.2f%%, %u distinct colours",
                image.width, image.height, static_cast<double>(statistics.meanLuminance),
                static_cast<double>(statistics.litFraction) * 100.0, static_cast<double>(statistics.cyanFraction) * 100.0,
                static_cast<double>(statistics.warmFraction) * 100.0, statistics.distinctColors);
  _report.Line(line);

  if (!_options.capture.empty())
  {
    if (!Neuron::SavePng(_options.capture, image))
    {
      _report.Line("FAIL capture: cannot write " + _options.capture.string());
      return ExitCode::Capture;
    }
    _report.Line("captured " + _options.capture.string());
  }
  if (!_options.compare.empty())
  {
    Neuron::Image reference;
    if (Neuron::LoadImageFile(_options.compare, reference))
    {
      std::snprintf(line, sizeof(line), "composition similarity to %s: %.3f", _options.compare.filename().string().c_str(),
                    static_cast<double>(Neuron::CompareComposition(image, reference)));
      _report.Line(line);
    }
    else
    {
      _report.Line("note: cannot read reference " + _options.compare.string());
    }
  }

  if (ReportDebugLayer(_session, _options, _report) > 0)
  {
    return ExitCode::DebugLayerErrors;
  }

  // A blank, black or flat frame fails: the scene must be lit, varied, and carry the cyan HUD and
  // the amber hangar and breach light.
  const bool imageOk = statistics.meanLuminance > 0.02f && statistics.litFraction > 0.05f && statistics.distinctColors > 400 &&
                       statistics.cyanFraction > 0.001f && statistics.warmFraction > 0.0005f;
  if (!imageOk)
  {
    _report.Line("FAIL image check: the frame looks blank or incomplete");
    return ExitCode::ImageCheck;
  }
  _report.Line("PASS");
  return ExitCode::Success;
}

[[nodiscard]] ExitCode RunInteractive(Session& _session)
{
  _session.window.Show();
  Neuron::TickClock clock;
  auto previous = std::chrono::steady_clock::now();
  const auto start = previous;
  float framesPerSecond = 0.0f;
  MSG message{};
  while (true)
  {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
      if (message.message == WM_QUIT)
      {
        return ExitCode::Success;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (_session.window.DeviceLost())
    {
      return ExitCode::Render;
    }
    if (_session.window.IsMinimized())
    {
      WaitMessage();
      previous = std::chrono::steady_clock::now();
      continue;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count();
    previous = now;
    const std::uint32_t ticks = clock.Advance(static_cast<std::uint64_t>(elapsed));
    for (std::uint32_t i = 0; i < ticks; ++i)
    {
      _session.world.Tick();
    }
    if (elapsed > 0)
    {
      framesPerSecond = framesPerSecond * 0.95f + 0.05f * (1'000'000.0f / static_cast<float>(elapsed));
    }
    const bool capture = _session.window.TakeCaptureRequest();
    const float time = std::chrono::duration<float>(now - start).count();
    if (!_session.client.Render(_session.world.Snapshot(), time, capture, true, framesPerSecond))
    {
      MessageBoxW(_session.window.Handle(), L"The graphics device was lost.", L"Outpost: Frontier", MB_ICONERROR);
      return ExitCode::Render;
    }
    if (capture)
    {
      const std::filesystem::path path = ScreenshotPath();
      if (Neuron::SavePng(path, CapturedImage(_session.device)))
      {
        OutputDebugStringW((L"Outpost: captured " + path.wstring() + L"\n").c_str());
      }
    }
  }
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE _instance, _In_opt_ HINSTANCE /*_previous*/, _In_ LPWSTR /*_commandLine*/, _In_ int /*_show*/)
{
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  Outpost::LaunchOptions options;
  std::string error;
  const bool parsed = Outpost::ParseCommandLine(Arguments(), options, error);
  Report report(options.smokeTest || !parsed);
  if (!parsed)
  {
    report.Line(error);
    report.Line(Outpost::UsageText());
    return static_cast<int>(ExitCode::Usage);
  }
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  ExitCode result = ExitCode::Success;
  {
    auto session = std::make_unique<Session>();
    std::filesystem::path assetRoot = options.assets;
    if (assetRoot.empty())
    {
      // Beside the executable after the deploy step, or in the repository when run unbuilt; never
      // relative to the working directory.
      const auto found = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
      assetRoot = found.value_or(std::filesystem::path{});
    }
    // The smoke test always renders in a window of exactly --size, so its captures do not depend on
    // the monitor; an interactive session is borderless fullscreen unless asked otherwise.
    const Outpost::WindowMode mode =
      options.smokeTest || options.windowed ? Outpost::WindowMode::Windowed : Outpost::WindowMode::BorderlessFullscreen;
    if (!session->window.Create(_instance, options.width, options.height, mode))
    {
      report.Line("FAIL device: cannot create the window");
      result = ExitCode::Device;
    }
    else if (assetRoot.empty())
    {
      report.Line("FAIL scene: cannot find Assets/Scene.txt beside the executable or above it");
      result = ExitCode::Scene;
    }
    else
    {
      RECT client{};
      GetClientRect(session->window.Handle(), &client);
      Neuron::GraphicsDeviceDesc desc;
      desc.window = session->window.Handle();
      // The smoke test renders at exactly the requested size even when the desktop is smaller
      // than the window would be; DXGI scales the hidden window's presentation.
      desc.width = options.smokeTest ? options.width : static_cast<std::uint32_t>(client.right - client.left);
      desc.height = options.smokeTest ? options.height : static_cast<std::uint32_t>(client.bottom - client.top);
      desc.useWarp = options.warp;
      desc.enableDebugLayer = options.debugLayer;
      if (!session->device.Initialize(desc, error))
      {
        report.Line("FAIL device: " + error);
        result = ExitCode::Device;
      }
      else
      {
        report.Line("adapter: " + Narrow(session->device.AdapterName()) + (session->device.IsWarp() ? " (WARP)" : ""));
        report.Line("assets: " + assetRoot.string());
        if (!session->client.Initialize(session->device, assetRoot, error))
        {
          report.Line("FAIL scene: " + error);
          result = ExitCode::Scene;
        }
        else
        {
          for (const std::string& warning : session->client.Warnings())
          {
            report.Line("warning: " + warning);
          }
          report.Line("scene: " + std::to_string(session->client.Scene().instances.size()) + " instances, " +
                      std::to_string(session->client.Assets().ModelCount()) + " models, " +
                      std::to_string(session->client.Assets().SurfaceVoxelCount()) + " surface voxels");
          session->world.Load(session->client.Scene());
          session->window.Attach(&session->device, &session->client);
          if (options.smokeTest)
          {
            result = RunSmokeTest(*session, options, report);
          }
          else
          {
            result = RunInteractive(*session);
            report.Line("interactive session ended after server tick " + std::to_string(session->world.TickCount()));
            if (ReportDebugLayer(*session, options, report) > 0 && result == ExitCode::Success)
            {
              result = ExitCode::DebugLayerErrors;
            }
          }
        }
      }
    }
    session->window.Attach(nullptr, nullptr);
  }
  report.Save(options.report);
  if (SUCCEEDED(com))
  {
    CoUninitialize();
  }
  return static_cast<int>(result);
}
