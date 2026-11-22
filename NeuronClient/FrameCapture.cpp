#include "FrameCapture.h"

#include "WindowsSdk.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_set>

namespace Neuron
{

using Microsoft::WRL::ComPtr;

bool SavePng(const std::filesystem::path& _path, const Image& _image)
{
  if (_image.width == 0 || _image.height == 0 || _image.rgba.size() < static_cast<std::size_t>(_image.width) * _image.height * 4)
  {
    return false;
  }
  std::error_code error;
  if (_path.has_parent_path())
  {
    std::filesystem::create_directories(_path.parent_path(), error);
  }
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(_path.c_str(), GENERIC_WRITE)) ||
      FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
      FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
      FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(_image.width, _image.height)))
  {
    return false;
  }
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA)
  {
    return false;
  }
  std::vector<std::uint8_t> bgra(_image.rgba.size());
  for (std::size_t i = 0; i + 3 < bgra.size(); i += 4)
  {
    bgra[i + 0] = _image.rgba[i + 2];
    bgra[i + 1] = _image.rgba[i + 1];
    bgra[i + 2] = _image.rgba[i + 0];
    bgra[i + 3] = 255;
  }
  const UINT stride = _image.width * 4;
  return SUCCEEDED(frame->WritePixels(_image.height, stride, static_cast<UINT>(bgra.size()), bgra.data())) && SUCCEEDED(frame->Commit()) &&
         SUCCEEDED(encoder->Commit());
}

bool LoadImageFile(const std::filesystem::path& _path, Image& _outImage)
{
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateDecoderFromFilename(_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom)))
  {
    return false;
  }
  UINT width = 0;
  UINT height = 0;
  if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0)
  {
    return false;
  }
  _outImage.width = width;
  _outImage.height = height;
  _outImage.rgba.resize(static_cast<std::size_t>(width) * height * 4);
  return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(_outImage.rgba.size()), _outImage.rgba.data()));
}

ImageStatistics MeasureImage(const Image& _image)
{
  ImageStatistics statistics;
  const std::size_t pixelCount = static_cast<std::size_t>(_image.width) * _image.height;
  if (pixelCount == 0 || _image.rgba.size() < pixelCount * 4)
  {
    return statistics;
  }
  double luminance = 0.0;
  std::size_t lit = 0;
  std::size_t cyan = 0;
  std::size_t warm = 0;
  std::unordered_set<std::uint32_t> colors;
  for (std::size_t i = 0; i < pixelCount; ++i)
  {
    const int r = _image.rgba[i * 4 + 0];
    const int g = _image.rgba[i * 4 + 1];
    const int b = _image.rgba[i * 4 + 2];
    const double y = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0;
    luminance += y;
    lit += y > 0.08 ? 1u : 0u;
    cyan += (g > 110 && b > 110 && r < g - 50) ? 1u : 0u;
    warm += (r > 140 && r > b + 70) ? 1u : 0u;
    colors.insert(static_cast<std::uint32_t>((r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8)));
  }
  statistics.meanLuminance = static_cast<float>(luminance / static_cast<double>(pixelCount));
  statistics.litFraction = static_cast<float>(lit) / static_cast<float>(pixelCount);
  statistics.cyanFraction = static_cast<float>(cyan) / static_cast<float>(pixelCount);
  statistics.warmFraction = static_cast<float>(warm) / static_cast<float>(pixelCount);
  statistics.distinctColors = static_cast<std::uint32_t>(colors.size());
  return statistics;
}

namespace
{

constexpr int GRID_WIDTH = 48;
constexpr int GRID_HEIGHT = 27;
constexpr std::size_t GRID_CELLS = static_cast<std::size_t>(GRID_WIDTH) * static_cast<std::size_t>(GRID_HEIGHT);

struct Lab
{
  float l;
  float a;
  float b;
};

[[nodiscard]] float SrgbToLinear(float _value) noexcept
{
  return _value <= 0.04045f ? _value / 12.92f : std::pow((_value + 0.055f) / 1.055f, 2.4f);
}

// Box-filters the image to the comparison grid in an opponent colour space with a cube-root
// lightness, which is close enough to CIELAB for a composition check.
[[nodiscard]] std::array<Lab, GRID_CELLS> Downsample(const Image& _image)
{
  std::array<Lab, GRID_CELLS> grid{};
  std::array<int, GRID_CELLS> counts{};
  for (std::uint32_t y = 0; y < _image.height; ++y)
  {
    const int gy = static_cast<int>(static_cast<std::uint64_t>(y) * GRID_HEIGHT / _image.height);
    for (std::uint32_t x = 0; x < _image.width; ++x)
    {
      const int gx = static_cast<int>(static_cast<std::uint64_t>(x) * GRID_WIDTH / _image.width);
      const std::size_t i = (static_cast<std::size_t>(y) * _image.width + x) * 4;
      const float r = SrgbToLinear(_image.rgba[i] / 255.0f);
      const float g = SrgbToLinear(_image.rgba[i + 1] / 255.0f);
      const float b = SrgbToLinear(_image.rgba[i + 2] / 255.0f);
      const float lightness = std::cbrt(0.2126f * r + 0.7152f * g + 0.0722f * b);
      Lab& cell = grid[static_cast<std::size_t>(gy) * GRID_WIDTH + static_cast<std::size_t>(gx)];
      cell.l += lightness;
      cell.a += std::cbrt(r) - std::cbrt(g);
      cell.b += std::cbrt(g) * 0.5f + std::cbrt(r) * 0.5f - std::cbrt(b);
      ++counts[static_cast<std::size_t>(gy) * GRID_WIDTH + static_cast<std::size_t>(gx)];
    }
  }
  for (std::size_t i = 0; i < grid.size(); ++i)
  {
    const float n = static_cast<float>(std::max(counts[i], 1));
    grid[i] = Lab{grid[i].l / n, grid[i].a / n, grid[i].b / n};
  }
  return grid;
}

} // namespace

float CompareComposition(const Image& _a, const Image& _b)
{
  if (_a.width == 0 || _b.width == 0)
  {
    return 0.0f;
  }
  const auto gridA = Downsample(_a);
  const auto gridB = Downsample(_b);
  double distance = 0.0;
  for (std::size_t i = 0; i < gridA.size(); ++i)
  {
    const float dl = gridA[i].l - gridB[i].l;
    const float da = gridA[i].a - gridB[i].a;
    const float db = gridA[i].b - gridB[i].b;
    distance += std::sqrt(dl * dl + da * da + db * db);
  }
  const double mean = distance / static_cast<double>(gridA.size());
  return static_cast<float>(std::clamp(1.0 - mean * 2.0, 0.0, 1.0));
}

PixelDifference ComparePixels(const Image& _a, const Image& _b)
{
  PixelDifference result;
  if (_a.width != _b.width || _a.height != _b.height || _a.rgba.size() != _b.rgba.size())
  {
    result.differingPixels = std::max(_a.width * _a.height, _b.width * _b.height);
    result.largestChannelDifference = 255;
    result.maxX = std::max(_a.width, _b.width) - 1;
    result.maxY = std::max(_a.height, _b.height) - 1;
    return result;
  }
  result.minX = _a.width;
  result.minY = _a.height;
  for (std::size_t pixel = 0; pixel + 3 < _a.rgba.size(); pixel += 4)
  {
    bool differs = false;
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
      const int delta = std::abs(int{_a.rgba[pixel + channel]} - int{_b.rgba[pixel + channel]});
      if (delta != 0)
      {
        differs = true;
        result.largestChannelDifference = std::max(result.largestChannelDifference, static_cast<std::uint8_t>(delta));
      }
    }
    if (differs)
    {
      const auto index = static_cast<std::uint32_t>(pixel / 4);
      const std::uint32_t x = index % _a.width;
      const std::uint32_t y = index / _a.width;
      result.minX = std::min(result.minX, x);
      result.minY = std::min(result.minY, y);
      result.maxX = std::max(result.maxX, x);
      result.maxY = std::max(result.maxY, y);
      ++result.differingPixels;
    }
  }
  if (result.differingPixels == 0)
  {
    result.minX = 0;
    result.minY = 0;
  }
  return result;
}

std::string DescribeImage(std::uint32_t _width, std::uint32_t _height, const ImageStatistics& _statistics)
{
  char line[192]{};
  std::snprintf(line, sizeof(line), "image %ux%u: mean luminance %.3f, lit %.1f%%, cyan %.2f%%, warm %.2f%%, %u distinct colours", _width,
                _height, static_cast<double>(_statistics.meanLuminance), static_cast<double>(_statistics.litFraction) * 100.0,
                static_cast<double>(_statistics.cyanFraction) * 100.0, static_cast<double>(_statistics.warmFraction) * 100.0,
                _statistics.distinctColors);
  return line;
}

bool FindImageDescription(std::string_view _report, std::uint32_t& _outWidth, std::uint32_t& _outHeight, ImageStatistics& _outStatistics)
{
  std::size_t start = 0;
  while (start < _report.size())
  {
    std::size_t end = _report.find('\n', start);
    if (end == std::string_view::npos)
    {
      end = _report.size();
    }
    const std::string line(_report.substr(start, end - start));
    start = end + 1;
    unsigned width = 0;
    unsigned height = 0;
    float luminance = 0.0f;
    float lit = 0.0f;
    float cyan = 0.0f;
    float warm = 0.0f;
    unsigned colors = 0;
    if (sscanf_s(line.c_str(), "image %ux%u: mean luminance %f, lit %f%%, cyan %f%%, warm %f%%, %u distinct colours", &width, &height,
                 &luminance, &lit, &cyan, &warm, &colors) == 7)
    {
      _outWidth = width;
      _outHeight = height;
      _outStatistics = ImageStatistics{luminance, lit / 100.0f, cyan / 100.0f, warm / 100.0f, colors};
      return true;
    }
  }
  return false;
}

std::string CompareStatistics(const ImageStatistics& _actual, const ImageStatistics& _expected, const StatisticsTolerance& _tolerance)
{
  std::string differences;
  const auto check =
    [&differences](const char* _name, float _actualValue, float _expectedValue, float _allowed, float _scale, int _decimals)
  {
    if (std::abs(_actualValue - _expectedValue) > _allowed)
    {
      char clause[128]{};
      std::snprintf(clause, sizeof(clause), "%s%s %.*f, expected %.*f", differences.empty() ? "" : "; ", _name, _decimals,
                    static_cast<double>(_actualValue * _scale), _decimals, static_cast<double>(_expectedValue * _scale));
      differences += clause;
    }
  };
  check("mean luminance", _actual.meanLuminance, _expected.meanLuminance, _tolerance.meanLuminance, 1.0f, 3);
  check("lit %", _actual.litFraction, _expected.litFraction, _tolerance.fractionPoints, 100.0f, 2);
  check("cyan %", _actual.cyanFraction, _expected.cyanFraction, _tolerance.fractionPoints, 100.0f, 2);
  check("warm %", _actual.warmFraction, _expected.warmFraction, _tolerance.fractionPoints, 100.0f, 2);
  check("distinct colours", static_cast<float>(_actual.distinctColors), static_cast<float>(_expected.distinctColors),
        static_cast<float>(_expected.distinctColors) * _tolerance.distinctColorsShare, 1.0f, 0);
  return differences;
}

} // namespace Neuron
