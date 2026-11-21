#include "Random.h"

#include <cmath>

namespace Neuron
{

Pcg32::Pcg32(std::uint64_t _seed, std::uint64_t _stream) noexcept
  : m_increment((_stream << 1u) | 1u)
{
  (void)Next();
  m_state += _seed;
  (void)Next();
}

std::uint32_t Pcg32::Next() noexcept
{
  const std::uint64_t old = m_state;
  m_state = old * 6364136223846793005ull + m_increment;
  const auto xorShifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
  const auto rotation = static_cast<std::uint32_t>(old >> 59u);
  return (xorShifted >> rotation) | (xorShifted << ((32u - rotation) & 31u));
}

float Pcg32::NextFloat() noexcept
{
  return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f);
}

float Pcg32::Range(float _low, float _high) noexcept
{
  return _low + (_high - _low) * NextFloat();
}

int Pcg32::RangeInt(int _low, int _highInclusive) noexcept
{
  const auto span = static_cast<std::uint32_t>(_highInclusive - _low + 1);
  return _low + static_cast<int>(Next() % span);
}

float HashToUnit(int _x, int _y, int _z, std::uint32_t _seed) noexcept
{
  std::uint32_t h = _seed ^ 0x9E3779B9u;
  h ^= static_cast<std::uint32_t>(_x) * 0x85EBCA6Bu;
  h = (h << 13) | (h >> 19);
  h ^= static_cast<std::uint32_t>(_y) * 0xC2B2AE35u;
  h = (h << 17) | (h >> 15);
  h ^= static_cast<std::uint32_t>(_z) * 0x27D4EB2Fu;
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

float ValueNoise3(float _x, float _y, float _z, std::uint32_t _seed) noexcept
{
  const float fx = std::floor(_x);
  const float fy = std::floor(_y);
  const float fz = std::floor(_z);
  const int ix = static_cast<int>(fx);
  const int iy = static_cast<int>(fy);
  const int iz = static_cast<int>(fz);
  const auto smooth = [](float _t) noexcept { return _t * _t * (3.0f - 2.0f * _t); };
  const float tx = smooth(_x - fx);
  const float ty = smooth(_y - fy);
  const float tz = smooth(_z - fz);
  const auto lerp = [](float _a, float _b, float _t) noexcept { return _a + (_b - _a) * _t; };

  float layer[2];
  for (int dz = 0; dz < 2; ++dz)
  {
    const float c00 = HashToUnit(ix, iy, iz + dz, _seed);
    const float c10 = HashToUnit(ix + 1, iy, iz + dz, _seed);
    const float c01 = HashToUnit(ix, iy + 1, iz + dz, _seed);
    const float c11 = HashToUnit(ix + 1, iy + 1, iz + dz, _seed);
    layer[dz] = lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
  }
  return lerp(layer[0], layer[1], tz);
}

float FractalNoise3(float _x, float _y, float _z, int _octaves, std::uint32_t _seed) noexcept
{
  float sum = 0.0f;
  float amplitude = 1.0f;
  float total = 0.0f;
  float frequency = 1.0f;
  for (int octave = 0; octave < _octaves; ++octave)
  {
    sum += amplitude * ValueNoise3(_x * frequency, _y * frequency, _z * frequency, _seed + static_cast<std::uint32_t>(octave) * 101u);
    total += amplitude;
    amplitude *= 0.5f;
    frequency *= 2.0f;
  }
  return sum / total;
}

} // namespace Neuron
