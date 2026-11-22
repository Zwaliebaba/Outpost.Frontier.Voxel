#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

/// A tightly packed RGBA8 image, top row first, sRGB-encoded.
struct Image
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
};

/// Writes a PNG through WIC (Windows SDK). The caller has initialized COM on this thread.
[[nodiscard]] bool SavePng(const std::filesystem::path& _path, const Image& _image);

/// Reads any WIC-decodable image (PNG, JPEG, ...) into RGBA8.
[[nodiscard]] bool LoadImageFile(const std::filesystem::path& _path, Image& _outImage);

/// Simple statistics that tell a rendered frame from a blank or broken one.
struct ImageStatistics
{
  float meanLuminance = 0.0f;       // 0..1
  float litFraction = 0.0f;         // pixels brighter than the background
  float cyanFraction = 0.0f;        // HUD-accent pixels: blue-green clearly above red
  float warmFraction = 0.0f;        // amber light: red clearly above blue
  std::uint32_t distinctColors = 0; // after quantizing to 4 bits per channel
};

[[nodiscard]] ImageStatistics MeasureImage(const Image& _image);

/// The one-line form a smoke report stores: "image WxH: mean luminance 0.146, lit 26.8%, ...".
[[nodiscard]] std::string DescribeImage(std::uint32_t _width, std::uint32_t _height, const ImageStatistics& _statistics);

/// Reads the line DescribeImage writes, found anywhere in a report. False if there is none.
[[nodiscard]] bool FindImageDescription(std::string_view _report, std::uint32_t& _outWidth, std::uint32_t& _outHeight,
                                        ImageStatistics& _outStatistics);

/// How far a frame's statistics may sit from a stored report's and still count as the same picture.
/// The defaults pass a re-run on another adapter and fail a moved camera or a missing selection.
struct StatisticsTolerance
{
  float meanLuminance = 0.005f;
  float fractionPoints = 0.003f;     // lit, cyan and warm, as fractions of the frame
  float distinctColorsShare = 0.05f; // as a share of the expected count
};

/// Empty when the statistics agree within the tolerance; otherwise what differs, one clause each.
[[nodiscard]] std::string CompareStatistics(const ImageStatistics& _actual, const ImageStatistics& _expected,
                                            const StatisticsTolerance& _tolerance = {});

/// A coarse similarity between two images of any size, 0..1: both are box-filtered to a 48x27
/// grid of Lab-ish colours and compared cell by cell. It is a sanity check for the composition,
/// not a perceptual metric.
[[nodiscard]] float CompareComposition(const Image& _a, const Image& _b);

/// An exact comparison of two images of the same size: how many pixels differ in any channel, and
/// the largest difference in one channel. Images of different sizes differ in every pixel.
struct PixelDifference
{
  std::uint32_t differingPixels = 0;
  std::uint8_t largestChannelDifference = 0;
  // The smallest rectangle holding every differing pixel, inclusive; meaningful only when some differ.
  std::uint32_t minX = 0;
  std::uint32_t minY = 0;
  std::uint32_t maxX = 0;
  std::uint32_t maxY = 0;
};

[[nodiscard]] PixelDifference ComparePixels(const Image& _a, const Image& _b);

} // namespace Neuron
