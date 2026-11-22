// Outpost.exe: the client with the server's logic in-process (ADR-001). The server half — Universe and
// TickClock — owns the game state and publishes it through an Observer; the client half —
// TacticalClient — builds its own picture from those messages and draws it, and sends its flight
// commands back. The two meet only in the loopback transport (ADR-008, plan tasks F4 and 1.4).
//
// With --smoke-test it initializes, loads the scene, renders a few frames, checks the last one and
// exits with an ExitCode, which is what CI and the README's verification steps run.

#include "CommandLine.h"
#include "MainWindow.h"

#include "Commands.h"

#include "FileSys.h"
#include "FrameCapture.h"
#include "GraphicsDevice.h"
#include "InputScript.h"
#include "LoopbackTransport.h"
#include "Observer.h"
#include "TacticalClient.h"
#include "TickClock.h"
#include "VoxFile.h"
#include "VoxelMesher.h"
#include "Universe.h"

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
  Outpost::Universe universe;
  Outpost::Observer observer;
  Neuron::LoopbackTransport transport;
};

/// How often the client's prediction of its own ship matched the server, tick for tick.
struct PredictionTally
{
  std::uint64_t matched = 0;
  std::uint64_t corrected = 0; // ticks the server diverged on and corrected
  std::uint64_t mismatched = 0;
  std::uint64_t firstMismatchTick = 0;
};

/// The server publishes what changed and the client takes it in: the hand-over between the halves,
/// after every tick. False, with the reason, if a message failed.
[[nodiscard]] bool Exchange(Session& _session, std::string& _outError)
{
  if (!_session.observer.Publish(_session.universe, _session.transport))
  {
    _outError = "the server could not encode or send a message";
    return false;
  }
  if (!_session.client.Receive(_session.transport))
  {
    _outError = _session.client.ProtocolError();
    return false;
  }
  return true;
}

/// One tick of the whole game, in the order the halves meet (plan task 1.4): the client samples its
/// input, predicts the tick and sends its command; the server takes the command in, runs the tick
/// and publishes; the client takes that in. A tally, when given, compares the prediction with the
/// server's state for the tick before the server's correction can reach the client.
[[nodiscard]] bool RunTick(Session& _session, std::string& _outError, PredictionTally* _tally = nullptr)
{
  if (!_session.client.SendTick(_session.transport, Neuron::TickClock::TICKS_PER_SECOND))
  {
    _outError = _session.client.ProtocolError();
    return false;
  }
  if (!Outpost::ReceiveCommands(_session.transport, _session.universe, _outError))
  {
    return false;
  }
  _session.universe.Tick();
  const Outpost::ObjectId ship = _session.universe.PlayerShip();
  const Outpost::FlightState* server = _session.universe.Flight(ship);
  const Outpost::FlightState* predicted = _session.client.OwnShip().StateAt(_session.universe.TickCount());
  if (_tally != nullptr && server != nullptr && predicted != nullptr && _session.client.OwnShip().Active())
  {
    if (*predicted == *server)
    {
      ++_tally->matched;
    }
    else if (_session.universe.DivergedAt(ship) == _session.universe.TickCount())
    {
      ++_tally->corrected;
    }
    else
    {
      _tally->firstMismatchTick = _tally->mismatched == 0 ? _session.universe.TickCount() : _tally->firstMismatchTick;
      ++_tally->mismatched;
    }
  }
  return Exchange(_session, _outError);
}

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

/// Frame-time samples of one quantity in milliseconds, summarized as percentiles (plan task F6).
[[nodiscard]] std::string Percentiles(const char* _name, std::vector<double> _samples)
{
  if (_samples.empty())
  {
    return std::string(_name) + ": no samples";
  }
  std::sort(_samples.begin(), _samples.end());
  const auto at = [&_samples](double _fraction)
  {
    const auto index = static_cast<std::size_t>(std::llround(_fraction * static_cast<double>(_samples.size() - 1)));
    return _samples[std::min(index, _samples.size() - 1)];
  };
  char line[256]{};
  std::snprintf(line, sizeof(line), "%s ms: p50 %.3f, p95 %.3f, p99 %.3f, max %.3f over %zu frames", _name, at(0.50), at(0.95), at(0.99),
                _samples.back(), _samples.size());
  return line;
}

/// Median wall time of ExtractSurface over a few runs, in milliseconds.
[[nodiscard]] double TimeSurfaceExtraction(const Neuron::VoxModel& _model, std::size_t& _outSurfaceVoxels)
{
  constexpr int RUNS = 5;
  std::vector<double> times;
  for (int run = 0; run < RUNS; ++run)
  {
    const auto start = std::chrono::steady_clock::now();
    const Neuron::VoxelSurface surface = Neuron::ExtractSurface(_model);
    times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    _outSurfaceVoxels = surface.voxels.size();
  }
  std::sort(times.begin(), times.end());
  return times[times.size() / 2];
}

constexpr int BRICK_VOXELS = 16;

/// A copy of one 16-voxel brick of a model, at a brick-aligned origin.
[[nodiscard]] Neuron::VoxModel CopyBrick(const Neuron::VoxModel& _model, int _originX, int _originY, int _originZ)
{
  Neuron::VoxModel brick(BRICK_VOXELS, BRICK_VOXELS, BRICK_VOXELS);
  for (int z = 0; z < BRICK_VOXELS; ++z)
  {
    for (int y = 0; y < BRICK_VOXELS; ++y)
    {
      for (int x = 0; x < BRICK_VOXELS; ++x)
      {
        brick.Set(x, y, z, _model.At(_originX + x, _originY + y, _originZ + z));
      }
    }
  }
  return brick;
}

/// Solid voxels of the brick at an origin that have an empty neighbour in the whole model.
[[nodiscard]] int CountBrickSurface(const Neuron::VoxModel& _model, int _originX, int _originY, int _originZ)
{
  int count = 0;
  for (int z = _originZ; z < _originZ + BRICK_VOXELS; ++z)
  {
    for (int y = _originY; y < _originY + BRICK_VOXELS; ++y)
    {
      for (int x = _originX; x < _originX + BRICK_VOXELS; ++x)
      {
        if (_model.At(x, y, z) != 0 && (_model.At(x - 1, y, z) == 0 || _model.At(x + 1, y, z) == 0 || _model.At(x, y - 1, z) == 0 ||
                                        _model.At(x, y + 1, z) == 0 || _model.At(x, y, z - 1) == 0 || _model.At(x, y, z + 1) == 0))
        {
          ++count;
        }
      }
    }
  }
  return count;
}

/// How long surface extraction takes per shipped model, and for one 16-voxel brick, which is the
/// unit damage will re-extract (plan tasks F6 and 3.1, ADR-013).
void ReportSurfaceExtraction(const Outpost::SceneDefinition& _scene, const std::filesystem::path& _assetRoot, Report& _report)
{
  char line[256]{};
  // The brick-aligned brick with the most surface voxels in any shipped model: the worst case for
  // re-extracting one brick.
  Neuron::VoxModel busiestBrick;
  std::string busiestName;
  int busiestSurface = -1;
  for (const Outpost::ModelAsset& asset : _scene.models)
  {
    Neuron::VoxDocument document;
    if (Neuron::LoadVoxFile(_assetRoot / asset.file, document) != Neuron::VoxError::None)
    {
      continue;
    }
    std::size_t surfaceVoxels = 0;
    const double milliseconds = TimeSurfaceExtraction(document.model, surfaceVoxels);
    std::snprintf(line, sizeof(line), "extract surface: %s %ux%ux%u, %zu solid, %zu surface: %.3f ms", asset.name.c_str(),
                  document.model.SizeX(), document.model.SizeY(), document.model.SizeZ(), document.model.CountSolid(), surfaceVoxels,
                  milliseconds);
    _report.Line(line);
    const Neuron::VoxModel& model = document.model;
    for (int z = 0; z < static_cast<int>(model.SizeZ()); z += BRICK_VOXELS)
    {
      for (int y = 0; y < static_cast<int>(model.SizeY()); y += BRICK_VOXELS)
      {
        for (int x = 0; x < static_cast<int>(model.SizeX()); x += BRICK_VOXELS)
        {
          const int surface = CountBrickSurface(model, x, y, z);
          if (surface > busiestSurface)
          {
            busiestSurface = surface;
            busiestBrick = CopyBrick(model, x, y, z);
            busiestName = asset.name + " at " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
          }
        }
      }
    }
  }

  Neuron::VoxModel solidBrick(BRICK_VOXELS, BRICK_VOXELS, BRICK_VOXELS);
  for (int z = 0; z < BRICK_VOXELS; ++z)
  {
    for (int y = 0; y < BRICK_VOXELS; ++y)
    {
      for (int x = 0; x < BRICK_VOXELS; ++x)
      {
        solidBrick.Set(x, y, z, 1);
      }
    }
  }
  std::size_t surfaceVoxels = 0;
  double milliseconds = TimeSurfaceExtraction(solidBrick, surfaceVoxels);
  std::snprintf(line, sizeof(line), "extract surface: solid brick 16x16x16, %zu surface: %.3f ms", surfaceVoxels, milliseconds);
  _report.Line(line);

  if (busiestSurface >= 0)
  {
    milliseconds = TimeSurfaceExtraction(busiestBrick, surfaceVoxels);
    std::snprintf(line, sizeof(line),
                  "extract surface: busiest brick 16x16x16 (%s, %d surface in its model), %zu solid, %zu surface: %.3f ms",
                  busiestName.c_str(), busiestSurface, busiestBrick.CountSolid(), surfaceVoxels, milliseconds);
    _report.Line(line);
  }
}

[[nodiscard]] ExitCode RunSmokeTest(Session& _session, const Outpost::LaunchOptions& _options, const std::filesystem::path& _assetRoot,
                                    Report& _report)
{
  // A script runs the timeline one frame past its last input unless --frames says how long.
  Outpost::InputScript script;
  int frames = _options.frames;
  if (!_options.script.empty())
  {
    std::string error;
    if (!Outpost::InputScript::Load(_options.script, script, error))
    {
      _report.Line("FAIL script: " + error);
      return ExitCode::Usage;
    }
    const auto needed = static_cast<int>(script.LastTick()) + 1;
    if (_options.framesGiven && _options.frames < needed)
    {
      _report.Line("FAIL script: it runs to tick " + std::to_string(script.LastTick()) + ", past --frames " +
                   std::to_string(_options.frames));
      return ExitCode::Usage;
    }
    frames = std::max(frames, needed);
    _report.Line("script: " + _options.script.filename().string() + ", " + std::to_string(script.Events().size()) + " input(s) to tick " +
                 std::to_string(script.LastTick()));
  }

  std::vector<double> gpuFrame;
  std::vector<double> gpuShadows;
  std::vector<double> gpuScene;
  std::vector<double> gpuHud;
  std::vector<double> frameInterval;
  auto previous = std::chrono::steady_clock::now();

  // A fixed timeline: frame i is at i/60 s and advances the server by one tick, so the capture is
  // the same on every run and every machine. Each frame is drawn at its tick, not between ticks.
  PredictionTally tally;
  for (int frame = 0; frame < frames; ++frame)
  {
    // A tick's input happens at that tick's time, before the client samples it, so gestures time
    // out as they would live.
    const std::uint64_t tick = _session.universe.TickCount() + 1;
    for (const Outpost::InputEvent& event : script.EventsAt(tick))
    {
      _session.client.Apply(event, tick * 1000 / Neuron::TickClock::TICKS_PER_SECOND);
    }
    std::string protocolError;
    if (!RunTick(_session, protocolError, &tally))
    {
      _report.Line("FAIL protocol: " + protocolError + " during frame " + std::to_string(frame));
      return ExitCode::Protocol;
    }
    const bool last = frame + 1 == frames;
    const float time = static_cast<float>(frame) / 60.0f;
    if (!_session.client.Render(time, 1.0f, last, false, 60.0f))
    {
      _report.Line("FAIL render: the device was removed during frame " + std::to_string(frame));
      return ExitCode::Render;
    }
    const auto now = std::chrono::steady_clock::now();
    // The last frame captures and waits for the GPU, so it is not a representative interval.
    if (_options.benchmark && frame >= Outpost::BENCHMARK_WARMUP_FRAMES && !last)
    {
      frameInterval.push_back(std::chrono::duration<double, std::milli>(now - previous).count());
      const std::vector<double>& marks = _session.device.CompletedTimestampsMilliseconds();
      if (marks.size() > Outpost::TacticalClient::TIMESTAMP_FRAME_END)
      {
        gpuShadows.push_back(marks[Outpost::TacticalClient::TIMESTAMP_SHADOWS_END]);
        gpuScene.push_back(marks[Outpost::TacticalClient::TIMESTAMP_SCENE_END] - marks[Outpost::TacticalClient::TIMESTAMP_SHADOWS_END]);
        gpuHud.push_back(marks[Outpost::TacticalClient::TIMESTAMP_FRAME_END] - marks[Outpost::TacticalClient::TIMESTAMP_SCENE_END]);
        gpuFrame.push_back(marks.back());
      }
    }
    previous = now;
  }
  _session.device.WaitIdle();
  _report.Line("rendered " + std::to_string(frames) + " frame(s), server tick " + std::to_string(_session.universe.TickCount()));
  _report.Line(std::string("view: ") + (_session.client.View() == Outpost::ClientView::Flight ? "flight" : "tactical"));
  if (!_session.client.Selected().empty())
  {
    _report.Line("selected: " + _session.client.Selected());
  }
  // The client flies its own ship ahead of the server with the same StepShip (plan task 1.7): every
  // tick it predicted must be the server's, bit for bit, unless the server diverged and said so.
  _report.Line("prediction: " + std::to_string(tally.matched) + " tick(s) matched the server, " + std::to_string(tally.corrected) +
               " corrected after a divergence, " + std::to_string(tally.mismatched) + " mismatched");
  if (const Outpost::FlightState* own = _session.universe.Flight(_session.universe.PlayerShip()))
  {
    char flight[160]{};
    std::snprintf(flight, sizeof(flight), "own ship: at %.2f, %.2f, %.2f, %.2f m/s, %zu contact(s)", own->position.x, own->position.y,
                  own->position.z, static_cast<double>(own->SpeedMetresPerSecond()), _session.universe.Contacts().size());
    _report.Line(flight);
    const Outpost::Manoeuvre& order = _session.client.Input().Order();
    if (order.kind == Outpost::ManoeuvreKind::GoTo)
    {
      const double dx = order.target.x - own->position.x;
      const double dy = order.target.y - own->position.y;
      const double dz = order.target.z - own->position.z;
      std::snprintf(flight, sizeof(flight), "  order: go to %.1f, %.1f, %.1f, %.1f m away, %s", order.target.x, order.target.y,
                    order.target.z, std::sqrt(dx * dx + dy * dy + dz * dz),
                    _session.universe.Arrived(_session.universe.PlayerShip()) ? "arrived" : "flying");
      _report.Line(flight);
    }
    constexpr std::size_t CONTACTS_REPORTED = 4;
    for (std::size_t i = 0; i < std::min(CONTACTS_REPORTED, _session.universe.Contacts().size()); ++i)
    {
      const Outpost::ContactRecord& contact = _session.universe.Contacts()[i];
      const Outpost::UniverseObject* body = _session.universe.State().FindObject(contact.body);
      std::snprintf(flight, sizeof(flight), "  contact at tick %llu with %s at %.1f m/s, damage %.1f",
                    static_cast<unsigned long long>(contact.tick), body != nullptr ? body->name.c_str() : "?",
                    static_cast<double>(contact.relativeSpeedMetresPerSecond), static_cast<double>(contact.damageToShip));
      _report.Line(flight);
    }
  }
  if (tally.mismatched > 0)
  {
    _report.Line("FAIL prediction: the client's own ship left the server's at tick " + std::to_string(tally.firstMismatchTick));
    return ExitCode::Prediction;
  }
  const Outpost::UniverseSnapshot& told = _session.client.Universe();
  _report.Line("client told of " + std::to_string(told.objects.size()) + " object(s), " + std::to_string(told.ships.size()) + " ship(s), " +
               std::to_string(told.objectives.size()) + " objective(s), " + std::to_string(told.resources.size()) +
               " resource(s), as of tick " + std::to_string(told.tick));
  if (_options.benchmark)
  {
    _report.Line("benchmark: " + std::to_string(_options.width) + "x" + std::to_string(_options.height) +
                 ", v-sync off, shadow map every frame (ADR-010), " + std::to_string(Outpost::BENCHMARK_WARMUP_FRAMES) +
                 " warm-up frame(s) not measured");
    _report.Line(Percentiles("gpu frame", gpuFrame));
    _report.Line(Percentiles("gpu shadow pass", gpuShadows));
    _report.Line(Percentiles("gpu scene pass", gpuScene));
    _report.Line(Percentiles("gpu hud", gpuHud));
    _report.Line(Percentiles("cpu frame interval", frameInterval));
    ReportSurfaceExtraction(_session.client.Scene(), _assetRoot, _report);
  }

  const Neuron::Image image = CapturedImage(_session.device);
  const Neuron::ImageStatistics statistics = Neuron::MeasureImage(image);
  char line[256]{};
  _report.Line(Neuron::DescribeImage(image.width, image.height, statistics));

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
      const Neuron::PixelDifference difference = Neuron::ComparePixels(image, reference);
      std::snprintf(line, sizeof(line), "pixels differing from %s: %u, largest channel difference %u, within %u,%u to %u,%u",
                    _options.compare.filename().string().c_str(), difference.differingPixels,
                    static_cast<unsigned>(difference.largestChannelDifference), difference.minX, difference.minY, difference.maxX,
                    difference.maxY);
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
  // the amber hangar and breach light. A chase view shows part of the yard against open space, so
  // it is less varied than the whole yard: the default flight frame has about 410 colours where the
  // tactical one has about 725, and a view out past the yard fewer still.
  const std::uint32_t minimumColors = _session.client.View() == Outpost::ClientView::Flight ? 200 : 400;
  const bool imageOk = statistics.meanLuminance > 0.02f && statistics.litFraction > 0.05f && statistics.distinctColors > minimumColors &&
                       statistics.cyanFraction > 0.001f && statistics.warmFraction > 0.0005f;
  // A benchmark may run a generated scene with no shipyard in view (--assets), so its image is
  // reported and not judged.
  if (!imageOk && !_options.benchmark)
  {
    _report.Line("FAIL image check: the frame looks blank or incomplete");
    return ExitCode::ImageCheck;
  }
  if (!_options.expect.empty())
  {
    std::string expected;
    std::uint32_t expectedWidth = 0;
    std::uint32_t expectedHeight = 0;
    Neuron::ImageStatistics expectedStatistics;
    if (!Neuron::ReadTextFile(_options.expect, expected) ||
        !Neuron::FindImageDescription(expected, expectedWidth, expectedHeight, expectedStatistics))
    {
      _report.Line("FAIL expect: no image line in " + _options.expect.string());
      return ExitCode::Usage;
    }
    if (expectedWidth != image.width || expectedHeight != image.height)
    {
      _report.Line("FAIL expect: the frame is " + std::to_string(image.width) + "x" + std::to_string(image.height) + ", the report's " +
                   std::to_string(expectedWidth) + "x" + std::to_string(expectedHeight));
      return ExitCode::Mismatch;
    }
    const std::string differences = Neuron::CompareStatistics(statistics, expectedStatistics);
    if (!differences.empty())
    {
      _report.Line("FAIL expect: " + differences);
      return ExitCode::Mismatch;
    }
    _report.Line("matches " + _options.expect.filename().string());
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
    // Wall time becomes ticks here and nowhere else (AGENTS.md R20).
    const std::uint32_t ticks = clock.Advance(static_cast<std::uint64_t>(elapsed));
    for (std::uint32_t i = 0; i < ticks; ++i)
    {
      std::string protocolError;
      if (!RunTick(_session, protocolError))
      {
        MessageBoxW(_session.window.Handle(), L"A message between the server and the client failed.", L"Outpost: Frontier", MB_ICONERROR);
        return ExitCode::Protocol;
      }
    }
    if (elapsed > 0)
    {
      framesPerSecond = framesPerSecond * 0.95f + 0.05f * (1'000'000.0f / static_cast<float>(elapsed));
    }
    const float time = std::chrono::duration<float>(now - start).count();
    // The capture command is read after the frame that saw it: this frame captures.
    const bool capture = _session.client.TakeCaptureRequest();
    if (!_session.client.Render(time, clock.Interpolation(), capture, true, framesPerSecond))
    {
      MessageBoxW(_session.window.Handle(), L"The graphics device was lost.", L"Outpost: Frontier", MB_ICONERROR);
      return ExitCode::Render;
    }
    if (_session.client.TakeFullscreenRequest())
    {
      _session.window.ToggleMode();
    }
    _session.window.ConfineCursor(_session.client.WantsCursorConfined());
    if (_session.client.TakeQuitRequest())
    {
      _session.window.ConfineCursor(false);
      PostMessageW(_session.window.Handle(), WM_CLOSE, 0, 0);
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
          // The server loads its own content; it is never handed the client's (ADR-009).
          if (!session->universe.LoadContent(assetRoot, error))
          {
            report.Line("FAIL server content: " + error);
            result = ExitCode::Scene;
          }
          else
          {
            report.Line("server: " + std::to_string(session->universe.State().objects.size()) + " objects");
            // The client is the observer that flies the player's ship, and is told what exists
            // before the first tick so that it can predict that tick.
            session->observer.AssignShip(session->universe.PlayerShip());
            if (!Exchange(*session, error))
            {
              report.Line("FAIL protocol: " + error + " before the first tick");
              result = ExitCode::Protocol;
            }
            else
            {
              report.Line("first publish: " + std::to_string(session->observer.LastMessageCount()) + " message(s)");
            }
          }
          session->window.Attach(&session->device, &session->client);
          if (result == ExitCode::Success && options.smokeTest)
          {
            result = RunSmokeTest(*session, options, assetRoot, report);
          }
          else if (result == ExitCode::Success)
          {
            result = RunInteractive(*session);
            report.Line("interactive session ended after server tick " + std::to_string(session->universe.TickCount()));
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
