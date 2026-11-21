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
  Capture = 7
};

struct LaunchOptions
{
  bool smokeTest = false;  // initialize, load, render, verify, exit
  bool warp = false;       // the WARP software rasterizer instead of the GPU
  bool debugLayer = false; // the D3D12 debug layer (on by default in Debug builds)
  bool vsync = true;
  bool windowed = false;      // a framed window of --size instead of borderless fullscreen
  int frames = 3;             // smoke test: frames to render before capturing the last
  std::uint32_t width = 1920; // client area when windowed, and the smoke test's render size
  std::uint32_t height = 1080;
  std::filesystem::path capture; // PNG of the last smoke-test frame
  std::filesystem::path compare; // reference image for a composition score
  std::filesystem::path report;  // text report of the smoke test
  std::filesystem::path assets;  // override for the Assets directory
};

/// Parses the arguments after the program name. On failure _outError says why.
[[nodiscard]] bool ParseCommandLine(const std::vector<std::wstring>& _arguments, LaunchOptions& _outOptions, std::string& _outError);

[[nodiscard]] const char* UsageText() noexcept;

} // namespace Outpost
