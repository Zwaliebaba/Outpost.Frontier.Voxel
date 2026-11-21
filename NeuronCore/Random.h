#pragma once

#include <cstdint>

namespace Neuron
{

/// PCG32 (O'Neill, pcg-random.org): a pinned, seedable generator. Two runs with the same seed
/// produce the same sequence on every build, which is what makes generated assets reproducible.
class Pcg32
{
public:
  explicit Pcg32(std::uint64_t _seed, std::uint64_t _stream = 0x2545F4914F6CDD1Dull) noexcept;

  [[nodiscard]] std::uint32_t Next() noexcept;

  /// Uniform in [0, 1).
  [[nodiscard]] float NextFloat() noexcept;

  /// Uniform in [_low, _high).
  [[nodiscard]] float Range(float _low, float _high) noexcept;

  /// Uniform integer in [_low, _highInclusive].
  [[nodiscard]] int RangeInt(int _low, int _highInclusive) noexcept;

private:
  std::uint64_t m_state = 0;
  std::uint64_t m_increment = 0;
};

/// A stateless integer hash of a lattice point, mapped to [0, 1).
[[nodiscard]] float HashToUnit(int _x, int _y, int _z, std::uint32_t _seed) noexcept;

/// Smoothly interpolated value noise in [0, 1).
[[nodiscard]] float ValueNoise3(float _x, float _y, float _z, std::uint32_t _seed) noexcept;

/// Fractal sum of value noise, normalized to [0, 1).
[[nodiscard]] float FractalNoise3(float _x, float _y, float _z, int _octaves, std::uint32_t _seed) noexcept;

} // namespace Neuron
