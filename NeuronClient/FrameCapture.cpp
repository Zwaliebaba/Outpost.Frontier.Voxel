#include "FrameCapture.h"

#include "WindowsSdk.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
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

} // namespace Neuron
