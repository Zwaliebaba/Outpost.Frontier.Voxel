#include "CommandLine.h"

#include <charconv>

namespace Outpost
{

namespace
{

[[nodiscard]] std::string Narrow(const std::wstring& _text)
{
  std::string result;
  for (const wchar_t character : _text)
  {
    result.push_back(character < 128 ? static_cast<char>(character) : '?');
  }
  return result;
}

[[nodiscard]] bool ParseInteger(const std::wstring& _text, int _low, int _high, int& _out)
{
  const std::string narrow = Narrow(_text);
  int value = 0;
  const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), value);
  if (error != std::errc{} || end != narrow.data() + narrow.size() || value < _low || value > _high)
  {
    return false;
  }
  _out = value;
  return true;
}

} // namespace

const char* UsageText() noexcept
{
  return "usage: Outpost [--windowed] [--size WxH] [--warp] [--debug-layer | --no-debug-layer] [--no-vsync]\n"
         "               [--assets directory]\n"
         "       Outpost --smoke-test [--frames N] [--size WxH] [--capture file.png] [--compare reference.png]\n"
         "               [--report file.txt] [--script file.txt] [--expect report.txt] [same device options]\n"
         "               --script plays input commands at fixed ticks and runs one frame past the last;\n"
         "               --expect fails the run when the frame's statistics leave a stored report's tolerance\n"
         "       Outpost --benchmark [--frames N] [smoke-test options]: times every frame on the GPU with the\n"
         "               shadow map redrawn each frame, and surface extraction per model; 600 frames by default\n"
         "Interactive sessions start borderless fullscreen unless --windowed; F11 or Alt+Enter switches.\n";
}

bool ParseCommandLine(const std::vector<std::wstring>& _arguments, LaunchOptions& _outOptions, std::string& _outError)
{
#ifdef _DEBUG
  _outOptions.debugLayer = true;
#endif
  for (std::size_t i = 0; i < _arguments.size(); ++i)
  {
    const std::wstring& argument = _arguments[i];
    const bool hasValue = i + 1 < _arguments.size();
    if (argument == L"--smoke-test")
    {
      _outOptions.smokeTest = true;
    }
    else if (argument == L"--benchmark")
    {
      _outOptions.smokeTest = true;
      _outOptions.benchmark = true;
    }
    else if (argument == L"--warp")
    {
      _outOptions.warp = true;
    }
    else if (argument == L"--debug-layer")
    {
      _outOptions.debugLayer = true;
    }
    else if (argument == L"--no-debug-layer")
    {
      _outOptions.debugLayer = false;
    }
    else if (argument == L"--windowed")
    {
      _outOptions.windowed = true;
    }
    else if (argument == L"--no-vsync")
    {
      _outOptions.vsync = false;
    }
    else if (argument == L"--frames" && hasValue)
    {
      if (!ParseInteger(_arguments[++i], 1, 10000, _outOptions.frames))
      {
        _outError = "--frames expects 1..10000";
        return false;
      }
      _outOptions.framesGiven = true;
    }
    else if (argument == L"--size" && hasValue)
    {
      const std::wstring& size = _arguments[++i];
      const std::size_t separator = size.find_first_of(L"xX");
      int width = 0;
      int height = 0;
      if (separator == std::wstring::npos || !ParseInteger(size.substr(0, separator), 320, 7680, width) ||
          !ParseInteger(size.substr(separator + 1), 240, 4320, height))
      {
        _outError = "--size expects WxH, e.g. 1920x1080";
        return false;
      }
      _outOptions.width = static_cast<std::uint32_t>(width);
      _outOptions.height = static_cast<std::uint32_t>(height);
    }
    else if (argument == L"--capture" && hasValue)
    {
      _outOptions.capture = _arguments[++i];
    }
    else if (argument == L"--compare" && hasValue)
    {
      _outOptions.compare = _arguments[++i];
    }
    else if (argument == L"--report" && hasValue)
    {
      _outOptions.report = _arguments[++i];
    }
    else if (argument == L"--script" && hasValue)
    {
      _outOptions.script = _arguments[++i];
    }
    else if (argument == L"--expect" && hasValue)
    {
      _outOptions.expect = _arguments[++i];
    }
    else if (argument == L"--assets" && hasValue)
    {
      _outOptions.assets = _arguments[++i];
    }
    else
    {
      _outError = "unknown or incomplete argument '" + Narrow(argument) + "'";
      return false;
    }
  }
  if (_outOptions.benchmark && !_outOptions.framesGiven)
  {
    _outOptions.frames = BENCHMARK_DEFAULT_FRAMES;
  }
  return true;
}

} // namespace Outpost
