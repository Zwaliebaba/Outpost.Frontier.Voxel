#pragma once

#include <cstdint>
#include <filesystem>
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

/// A coarse similarity between two images of any size, 0..1: both are box-filtered to a 48x27
/// grid of Lab-ish colours and compared cell by cell. It is a sanity check for the composition,
/// not a perceptual metric.
[[nodiscard]] float CompareComposition(const Image& _a, const Image& _b);

} // namespace Neuron
