#include "TickClock.h"

#include "CppUnitTest.h"

#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

namespace
{

constexpr std::uint64_t TICK = Neuron::TickClock::MICROSECONDS_PER_TICK;

} // namespace

TEST_CLASS(TickClockTests)
{
public:
  TEST_METHOD(WallTimeBecomesWholeTicks)
  {
    Neuron::TickClock clock;
    Assert::AreEqual(0u, clock.Advance(TICK - 1), L"not yet a whole tick");
    Assert::AreEqual(1u, clock.Advance(1));
    Assert::AreEqual(std::uint64_t{1}, clock.TickCount());
  }

  TEST_METHOD(TheRemainderCarriesIntoTheNextAdvance)
  {
    Neuron::TickClock clock;
    Assert::AreEqual(1u, clock.Advance(TICK + TICK / 2));
    Assert::AreEqual(1u, clock.Advance(TICK / 2), L"the half tick left over completes the next one");
    Assert::AreEqual(std::uint64_t{2}, clock.TickCount());
  }

  TEST_METHOD(AStallIsCaughtUpGentlyAndItsBacklogDropped)
  {
    Neuron::TickClock clock;
    Assert::AreEqual(Neuron::TickClock::MAX_TICKS_PER_ADVANCE, clock.Advance(TICK * 100));
    Assert::AreEqual(0u, clock.Advance(0), L"the rest of the stall is not replayed");
    Assert::AreEqual(std::uint64_t{Neuron::TickClock::MAX_TICKS_PER_ADVANCE}, clock.TickCount());
  }

  TEST_METHOD(InterpolationIsTheFractionOfTheNextTick)
  {
    Neuron::TickClock clock;
    Assert::AreEqual(0u, clock.Advance(TICK / 4));
    Assert::IsTrue(std::abs(clock.Interpolation() - 0.25f) < 1e-6f);
    Assert::AreEqual(1u, clock.Advance(TICK * 3 / 4));
    Assert::IsTrue(std::abs(clock.Interpolation()) < 1e-6f, L"a completed tick leaves nothing pending");
  }
};

} // namespace NeuronServerTests
