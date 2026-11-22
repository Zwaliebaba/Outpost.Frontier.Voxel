#include "FrameCapture.h"

#include <string>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

[[nodiscard]] Neuron::Image Flat(std::uint32_t _width, std::uint32_t _height, std::uint8_t _value)
{
  Neuron::Image image;
  image.width = _width;
  image.height = _height;
  image.rgba.assign(static_cast<std::size_t>(_width) * _height * 4, _value);
  return image;
}

} // namespace

TEST_CLASS(FrameCaptureTests)
{
public:
  TEST_METHOD(IdenticalImagesDifferInNoPixel)
  {
    const Neuron::PixelDifference difference = Neuron::ComparePixels(Flat(8, 4, 30), Flat(8, 4, 30));
    Assert::AreEqual(std::uint32_t{0}, difference.differingPixels);
    Assert::AreEqual(std::uint8_t{0}, difference.largestChannelDifference);
  }

  TEST_METHOD(EachChangedPixelCountsOnceWithItsLargestChannel)
  {
    const Neuron::Image reference = Flat(8, 4, 30);
    Neuron::Image changed = reference;
    changed.rgba[0] = 31;        // pixel 0, red, by 1
    changed.rgba[2] = 40;        // pixel 0, blue, by 10: still one pixel
    changed.rgba[4 * 9 + 1] = 0; // pixel 9, green, by 30
    const Neuron::PixelDifference difference = Neuron::ComparePixels(changed, reference);
    Assert::AreEqual(std::uint32_t{2}, difference.differingPixels);
    Assert::AreEqual(std::uint8_t{30}, difference.largestChannelDifference);
    Assert::AreEqual(std::uint32_t{0}, difference.minX, L"pixel 0 is at the top left");
    Assert::AreEqual(std::uint32_t{0}, difference.minY);
    Assert::AreEqual(std::uint32_t{1}, difference.maxX, L"pixel 9 of a row of 8 is x 1, y 1");
    Assert::AreEqual(std::uint32_t{1}, difference.maxY);
  }

  TEST_METHOD(ImagesOfDifferentSizesDifferEverywhere)
  {
    const Neuron::PixelDifference difference = Neuron::ComparePixels(Flat(8, 4, 30), Flat(4, 4, 30));
    Assert::AreEqual(std::uint32_t{32}, difference.differingPixels);
    Assert::AreEqual(std::uint8_t{255}, difference.largestChannelDifference);
  }

  TEST_METHOD(AReportsImageLineReadsBack)
  {
    const Neuron::ImageStatistics written{0.146f, 0.268f, 0.0192f, 0.0263f, 657};
    const std::string report = "adapter: WARP\n" + Neuron::DescribeImage(1920, 1080, written) + "\nPASS\n";
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Neuron::ImageStatistics read;
    Assert::IsTrue(Neuron::FindImageDescription(report, width, height, read));
    Assert::AreEqual(std::uint32_t{1920}, width);
    Assert::AreEqual(std::uint32_t{1080}, height);
    Assert::AreEqual(657u, read.distinctColors);
    Assert::IsTrue(Neuron::CompareStatistics(read, written).empty(), L"the printed precision is inside the tolerance");
  }

  TEST_METHOD(AReportWithoutAnImageLineIsRefused)
  {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Neuron::ImageStatistics read;
    Assert::IsFalse(Neuron::FindImageDescription("adapter: WARP\nFAIL device\n", width, height, read));
  }

  TEST_METHOD(StatisticsOutsideTheToleranceAreNamed)
  {
    const Neuron::ImageStatistics expected{0.146f, 0.268f, 0.0192f, 0.0263f, 657};
    Neuron::ImageStatistics actual = expected;
    actual.litFraction += 0.002f;
    actual.distinctColors = 680;
    Assert::IsTrue(Neuron::CompareStatistics(actual, expected).empty(), L"0.2 points of lit and 3.5 % more colours pass");
    actual.cyanFraction += 0.01f;
    actual.distinctColors = 700;
    const std::string differences = Neuron::CompareStatistics(actual, expected);
    Assert::IsTrue(differences.find("cyan") != std::string::npos);
    Assert::IsTrue(differences.find("colours") != std::string::npos);
    Assert::IsTrue(differences.find("lit") == std::string::npos);
  }
};

} // namespace NeuronClientTests
