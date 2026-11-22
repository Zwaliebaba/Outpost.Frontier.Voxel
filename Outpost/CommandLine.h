#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Outpost
{

/// Process exit codes. The smoke test's code says which stage failed.
enum class ExitCode : int
{
  Success = 0,
  Usage = 1,
  Device = 2,
  Scene = 3,
  Render = 4,
  DebugLayerErrors = 5,
  ImageCheck = 6,
  Capture = 7,
  Protocol = 8,    // a message between the halves could not be encoded, sent or decoded
  Mismatch = 9,    // the frame's statistics left the tolerance of the report given to --expect
  Prediction = 10, // the client's prediction of its own ship left the server's on a tick the server did not correct
  Save = 11        // the save could not be read, did not fit the content, or could not be written
};

// A benchmark's frame count unless --frames says otherwise, and the frames at its start that are
// not measured while caches, clocks and the swap chain settle.
inline constexpr int BENCHMARK_DEFAULT_FRAMES = 600;
inline constexpr int BENCHMARK_WARMUP_FRAMES = 60;

struct LaunchOptions
{
  bool smokeTest = false;  // initialize, load, render, verify, exit
  bool benchmark = false;  // a smoke test that also times frames and surface extraction (plan task F6)
  bool warp = false;       // the WARP software rasterizer instead of the GPU
  bool debugLayer = false; // the D3D12 debug layer (on by default in Debug builds)
  bool vsync = true;
  bool windowed = false;      // a framed window of --size instead of borderless fullscreen
  int frames = 3;             // smoke test: frames to render before capturing the last
  bool framesGiven = false;   // --frames was on the command line, so a script may not lengthen the run
  std::uint32_t width = 1920; // client area when windowed, and the smoke test's render size
  std::uint32_t height = 1080;
  std::filesystem::path capture; // PNG of the last smoke-test frame
  std::filesystem::path compare; // reference image for a composition score
  std::filesystem::path report;  // text report of the smoke test
  std::filesystem::path script;  // smoke test: input commands at fixed ticks (plan task F5)
  std::filesystem::path expect;  // smoke test: a stored report whose image statistics the frame must match
  std::filesystem::path assets;  // override for the Assets directory
  std::filesystem::path save;    // the save file; by default Saved Games\Outpost Frontier\Save.txt, none in a smoke test
};

/// Parses the arguments after the program name. On failure _outError says why.
[[nodiscard]] bool ParseCommandLine(const std::vector<std::wstring>& _arguments, LaunchOptions& _outOptions, std::string& _outError);

[[nodiscard]] const char* UsageText() noexcept;

} // namespace Outpost
