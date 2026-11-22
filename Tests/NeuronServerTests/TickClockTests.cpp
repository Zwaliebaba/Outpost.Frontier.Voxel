#include "TickClock.h"

#include "CppUnitTest.h"

#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

// The figures below are microseconds at the rate ADR-007 fixes; a tick is 16,666.67 of them.
static_assert(Neuron::TickClock::TICKS_PER_SECOND == 60);

TEST_CLASS(TickClockTests)
{
public:
  TEST_METHOD(WallTimeBecomesWholeTicks)
  {
    Neuron::TickClock clock;
    Assert::AreEqual(0u, clock.Advance(16'666), L"two thirds of a microsecond short of a tick");
    Assert::AreEqual(1u, clock.Advance(1));
    Assert::AreEqual(std::uint64_t{1}, clock.TickCount());
  }

  TEST_METHOD(TheRemainderCarriesIntoTheNextAdvance)
  {
    Neuron::TickClock clock;
    Assert::AreEqual(1u, clock.Advance(25'000), L"a tick and a half");
    Assert::AreEqual(1u, clock.Advance(8'334), L"the half tick left over completes the next one");
    Assert::AreEqual(std::uint64_t{2}, clock.TickCount());
  }

  TEST_METHOD(TenMinutesAreExactlyTheirTicks)
  {
    // A tick of a whole number of microseconds (16,666) would run fast and count 36,001 here.
    Neuron::TickClock clock;
    std::uint64_t ticks = 0;
    for (int frame = 0; frame < 60'000; ++frame)
    {
      ticks += clock.Advance(10'000);
    }
    Assert::AreEqual(std::uint64_t{36'000}, ticks);
    Assert::AreEqual(std::uint64_t{36'000}, clock.TickCount());
  }

  TEST_METHOD(AStallIsCaughtUpGentlyAndItsBacklogDropped)
  {
    Neuron::TickClock clock;
    Assert::AreEqual(Neuron::TickClock::MAX_TICKS_PER_ADVANCE, clock.Advance(10'000'000));
    Assert::AreEqual(0u, clock.Advance(0), L"the rest of the stall is not replayed");
    Assert::AreEqual(std::uint64_t{Neuron::TickClock::MAX_TICKS_PER_ADVANCE}, clock.TickCount());
  }

  TEST_METHOD(InterpolationIsTheFractionOfTheNextTick)
  {
    Neuron::TickClock clock;
    Assert::AreEqual(0u, clock.Advance(12'500));
    Assert::IsTrue(std::abs(clock.Interpolation() - 0.75f) < 1e-6f, L"three quarters of a tick");
    Assert::AreEqual(1u, clock.Advance(4'167));
    Assert::IsTrue(clock.Interpolation() < 1e-4f, L"a completed tick leaves almost nothing pending");
  }
};

} // namespace NeuronServerTests
