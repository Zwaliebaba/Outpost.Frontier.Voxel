#include "VoxelBuilder.h"

#include "Palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace AssetGen
{

namespace
{

[[nodiscard]] Int3 Low(Int3 _a, Int3 _b) noexcept
{
  return Int3{std::min(_a.x, _b.x), std::min(_a.y, _b.y), std::min(_a.z, _b.z)};
}

[[nodiscard]] Int3 High(Int3 _a, Int3 _b) noexcept
{
  return Int3{std::max(_a.x, _b.x), std::max(_a.y, _b.y), std::max(_a.z, _b.z)};
}

template <typename Fn> void ForBox(Int3 _a, Int3 _b, Fn&& _fn)
{
  const Int3 lo = Low(_a, _b);
  const Int3 hi = High(_a, _b);
  for (int z = lo.z; z <= hi.z; ++z)
  {
    for (int y = lo.y; y <= hi.y; ++y)
    {
      for (int x = lo.x; x <= hi.x; ++x)
      {
        _fn(x, y, z);
      }
    }
  }
}

// A 4x7 voxel font, top row first.
struct Glyph
{
  char character;
  std::array<const char*, 7> rows;
};

constexpr std::array<Glyph, 17> FONT = {{
  {'0', {"####", "#..#", "#..#", "#..#", "#..#", "#..#", "####"}},
  {'1', {".##.", "###.", ".##.", ".##.", ".##.", ".##.", "####"}},
  {'2', {"####", "...#", "...#", "####", "#...", "#...", "####"}},
  {'3', {"####", "...#", "...#", ".###", "...#", "...#", "####"}},
  {'4', {"#..#", "#..#", "#..#", "####", "...#", "...#", "...#"}},
  {'5', {"####", "#...", "#...", "####", "...#", "...#", "####"}},
  {'6', {"####", "#...", "#...", "####", "#..#", "#..#", "####"}},
  {'7', {"####", "...#", "..#.", ".#..", ".#..", ".#..", ".#.."}},
  {'8', {"####", "#..#", "#..#", "####", "#..#", "#..#", "####"}},
  {'9', {"####", "#..#", "#..#", "####", "...#", "...#", "####"}},
  {'F', {"####", "#...", "#...", "###.", "#...", "#...", "#..."}},
  {'B', {"###.", "#..#", "#..#", "###.", "#..#", "#..#", "###."}},
  {'S', {"####", "#...", "#...", "####", "...#", "...#", "####"}},
  {'T', {"####", ".##.", ".##.", ".##.", ".##.", ".##.", ".##."}},
  {'C', {"####", "#...", "#...", "#...", "#...", "#...", "####"}},
  {'-', {"....", "....", "....", "####", "....", "....", "...."}},
  {' ', {"....", "....", "....", "....", "....", "....", "...."}},
}};

[[nodiscard]] const Glyph* FindGlyph(char _c) noexcept
{
  for (const Glyph& glyph : FONT)
  {
    if (glyph.character == _c)
    {
      return &glyph;
    }
  }
  return nullptr;
}

// Hexagon "radius" of a point for a flat-topped hexagon.
[[nodiscard]] float HexDistance(float _u, float _v) noexcept
{
  const float au = std::fabs(_u);
  const float av = std::fabs(_v);
  return std::max(au * 0.8660254f + av * 0.5f, av);
}

} // namespace

VoxelBuilder::VoxelBuilder(int _sizeX, int _sizeY, int _sizeZ, std::uint32_t _seed)
  : m_model(static_cast<std::uint32_t>(_sizeX), static_cast<std::uint32_t>(_sizeY), static_cast<std::uint32_t>(_sizeZ)),
    m_seed(_seed),
    m_rng(_seed)
{
}

void VoxelBuilder::Box(Int3 _min, Int3 _max, std::uint8_t _color) noexcept
{
  ForBox(_min, _max, [&](int _x, int _y, int _z) { Set(_x, _y, _z, _color); });
}

void VoxelBuilder::Clear(Int3 _min, Int3 _max) noexcept
{
  Box(_min, _max, Neuron::VOX_EMPTY);
}

void VoxelBuilder::Shell(Int3 _min, Int3 _max, int _thickness, std::uint8_t _color) noexcept
{
  Box(_min, _max, _color);
  const Int3 lo = Low(_min, _max);
  const Int3 hi = High(_min, _max);
  Clear(Int3{lo.x + _thickness, lo.y + _thickness, lo.z + _thickness}, Int3{hi.x - _thickness, hi.y - _thickness, hi.z - _thickness});
}

void VoxelBuilder::CylinderX(float _centerY, float _centerZ, float _radius, int _x0, int _x1, std::uint8_t _color) noexcept
{
  const float r2 = _radius * _radius;
  ForBox(Int3{_x0, static_cast<int>(_centerY - _radius) - 1, static_cast<int>(_centerZ - _radius) - 1},
         Int3{_x1, static_cast<int>(_centerY + _radius) + 1, static_cast<int>(_centerZ + _radius) + 1},
         [&](int _x, int _y, int _z)
         {
           const float dy = static_cast<float>(_y) + 0.5f - _centerY;
           const float dz = static_cast<float>(_z) + 0.5f - _centerZ;
           if (dy * dy + dz * dz <= r2)
           {
             Set(_x, _y, _z, _color);
           }
         });
}

void VoxelBuilder::CylinderY(float _centerX, float _centerZ, float _radius, int _y0, int _y1, std::uint8_t _color) noexcept
{
  const float r2 = _radius * _radius;
  ForBox(Int3{static_cast<int>(_centerX - _radius) - 1, _y0, static_cast<int>(_centerZ - _radius) - 1},
         Int3{static_cast<int>(_centerX + _radius) + 1, _y1, static_cast<int>(_centerZ + _radius) + 1},
         [&](int _x, int _y, int _z)
         {
           const float dx = static_cast<float>(_x) + 0.5f - _centerX;
           const float dz = static_cast<float>(_z) + 0.5f - _centerZ;
           if (dx * dx + dz * dz <= r2)
           {
             Set(_x, _y, _z, _color);
           }
         });
}

void VoxelBuilder::CylinderZ(float _centerX, float _centerY, float _radius, int _z0, int _z1, std::uint8_t _color) noexcept
{
  const float r2 = _radius * _radius;
  ForBox(Int3{static_cast<int>(_centerX - _radius) - 1, static_cast<int>(_centerY - _radius) - 1, _z0},
         Int3{static_cast<int>(_centerX + _radius) + 1, static_cast<int>(_centerY + _radius) + 1, _z1},
         [&](int _x, int _y, int _z)
         {
           const float dx = static_cast<float>(_x) + 0.5f - _centerX;
           const float dy = static_cast<float>(_y) + 0.5f - _centerY;
           if (dx * dx + dy * dy <= r2)
           {
             Set(_x, _y, _z, _color);
           }
         });
}

void VoxelBuilder::Ellipsoid(float _cx, float _cy, float _cz, float _rx, float _ry, float _rz, std::uint8_t _color) noexcept
{
  ForBox(Int3{static_cast<int>(_cx - _rx) - 1, static_cast<int>(_cy - _ry) - 1, static_cast<int>(_cz - _rz) - 1},
         Int3{static_cast<int>(_cx + _rx) + 1, static_cast<int>(_cy + _ry) + 1, static_cast<int>(_cz + _rz) + 1},
         [&](int _x, int _y, int _z)
         {
           const float dx = (static_cast<float>(_x) + 0.5f - _cx) / _rx;
           const float dy = (static_cast<float>(_y) + 0.5f - _cy) / _ry;
           const float dz = (static_cast<float>(_z) + 0.5f - _cz) / _rz;
           if (dx * dx + dy * dy + dz * dz <= 1.0f)
           {
             Set(_x, _y, _z, _color);
           }
         });
}

void VoxelBuilder::Paint(Int3 _min, Int3 _max, std::uint8_t _color) noexcept
{
  ForBox(_min, _max,
         [&](int _x, int _y, int _z)
         {
           if (Solid(_x, _y, _z))
           {
             Set(_x, _y, _z, _color);
           }
         });
}

void VoxelBuilder::PaintPanels(Int3 _min, Int3 _max, Shade _shade, int _panelSize) noexcept
{
  const int span = _shade.high - _shade.low + 1;
  ForBox(_min, _max,
         [&](int _x, int _y, int _z)
         {
           if (!Solid(_x, _y, _z))
           {
             return;
           }
           const float h = Neuron::HashToUnit(_x / _panelSize, _y / _panelSize, _z / _panelSize, m_seed + _shade.family);
           const int shade = _shade.low + std::min(span - 1, static_cast<int>(h * static_cast<float>(span)));
           Set(_x, _y, _z, static_cast<std::uint8_t>(_shade.family + shade));
         });
}

void VoxelBuilder::Replace(Int3 _min, Int3 _max, std::uint8_t _from, std::uint8_t _to) noexcept
{
  ForBox(_min, _max,
         [&](int _x, int _y, int _z)
         {
           if (At(_x, _y, _z) == _from)
           {
             Set(_x, _y, _z, _to);
           }
         });
}

void VoxelBuilder::HazardStripes(Int3 _min, Int3 _max, int _period) noexcept
{
  ForBox(_min, _max,
         [&](int _x, int _y, int _z)
         {
           if (!Solid(_x, _y, _z))
           {
             return;
           }
           const int phase = ((_x + _y + _z) % (_period * 2) + _period * 2) % (_period * 2);
           Set(_x, _y, _z,
               phase < _period ? static_cast<std::uint8_t>(Outpost::PaletteIndex::HAZARD_YELLOW + 1)
                               : static_cast<std::uint8_t>(Outpost::PaletteIndex::HAZARD_DARK + 1));
         });
}

void VoxelBuilder::MirrorX() noexcept
{
  const int sx = SizeX();
  for (int z = 0; z < SizeZ(); ++z)
  {
    for (int y = 0; y < SizeY(); ++y)
    {
      for (int x = 0; x < sx / 2; ++x)
      {
        Set(sx - 1 - x, y, z, At(x, y, z));
      }
    }
  }
}

void VoxelBuilder::Truss(Int3 _min, Int3 _max, int _axis, int _bay, std::uint8_t _longeron, std::uint8_t _brace) noexcept
{
  const Int3 lo = Low(_min, _max);
  const Int3 hi = High(_min, _max);
  const auto component = [](const Int3& _p, int _i) noexcept { return _i == 0 ? _p.x : (_i == 1 ? _p.y : _p.z); };
  const auto make = [&](int _a, int _u, int _v) noexcept
  {
    Int3 p{0, 0, 0};
    const int u = (_axis + 1) % 3;
    const int v = (_axis + 2) % 3;
    (_axis == 0 ? p.x : (_axis == 1 ? p.y : p.z)) = _a;
    (u == 0 ? p.x : (u == 1 ? p.y : p.z)) = _u;
    (v == 0 ? p.x : (v == 1 ? p.y : p.z)) = _v;
    return p;
  };
  const int a0 = component(lo, _axis);
  const int a1 = component(hi, _axis);
  const int u0 = component(lo, (_axis + 1) % 3);
  const int u1 = component(hi, (_axis + 1) % 3);
  const int v0 = component(lo, (_axis + 2) % 3);
  const int v1 = component(hi, (_axis + 2) % 3);

  const auto line = [&](Int3 _p, Int3 _q, std::uint8_t _color) noexcept
  {
    const int steps = std::max({std::abs(_q.x - _p.x), std::abs(_q.y - _p.y), std::abs(_q.z - _p.z), 1});
    for (int i = 0; i <= steps; ++i)
    {
      const float t = static_cast<float>(i) / static_cast<float>(steps);
      Set(static_cast<int>(std::lround(static_cast<float>(_p.x) + static_cast<float>(_q.x - _p.x) * t)),
          static_cast<int>(std::lround(static_cast<float>(_p.y) + static_cast<float>(_q.y - _p.y) * t)),
          static_cast<int>(std::lround(static_cast<float>(_p.z) + static_cast<float>(_q.z - _p.z) * t)), _color);
    }
  };

  for (int a = a0; a <= a1; ++a)
  {
    Set(make(a, u0, v0).x, make(a, u0, v0).y, make(a, u0, v0).z, _longeron);
    Set(make(a, u1, v0).x, make(a, u1, v0).y, make(a, u1, v0).z, _longeron);
    Set(make(a, u0, v1).x, make(a, u0, v1).y, make(a, u0, v1).z, _longeron);
    Set(make(a, u1, v1).x, make(a, u1, v1).y, make(a, u1, v1).z, _longeron);
  }
  for (int a = a0; a + _bay <= a1; a += _bay)
  {
    const int b = a + _bay;
    // A ring at the start of the bay.
    line(make(a, u0, v0), make(a, u1, v0), _brace);
    line(make(a, u1, v0), make(a, u1, v1), _brace);
    line(make(a, u1, v1), make(a, u0, v1), _brace);
    line(make(a, u0, v1), make(a, u0, v0), _brace);
    // One diagonal on each face, alternating direction from bay to bay.
    const bool flip = ((a - a0) / _bay) % 2 == 1;
    line(make(a, flip ? u1 : u0, v0), make(b, flip ? u0 : u1, v0), _brace);
    line(make(a, flip ? u1 : u0, v1), make(b, flip ? u0 : u1, v1), _brace);
    line(make(a, u0, flip ? v1 : v0), make(b, u0, flip ? v0 : v1), _brace);
    line(make(a, u1, flip ? v1 : v0), make(b, u1, flip ? v0 : v1), _brace);
  }
  line(make(a1, u0, v0), make(a1, u1, v0), _brace);
  line(make(a1, u1, v0), make(a1, u1, v1), _brace);
  line(make(a1, u1, v1), make(a1, u0, v1), _brace);
  line(make(a1, u0, v1), make(a1, u0, v0), _brace);
}

void VoxelBuilder::TextFrontY(std::string_view _text, int _x, int _y, int _z, int _scale, std::uint8_t _color) noexcept
{
  int penX = _x;
  for (const char c : _text)
  {
    const Glyph* glyph = FindGlyph(c);
    if (glyph != nullptr)
    {
      for (int row = 0; row < 7; ++row)
      {
        for (int column = 0; column < 4; ++column)
        {
          if (glyph->rows[static_cast<std::size_t>(row)][column] != '#')
          {
            continue;
          }
          for (int sy = 0; sy < _scale; ++sy)
          {
            for (int sx = 0; sx < _scale; ++sx)
            {
              Set(penX + column * _scale + sx, _y, _z + (6 - row) * _scale + sy, _color);
            }
          }
        }
      }
    }
    penX += 5 * _scale;
  }
}

void VoxelBuilder::HexEmblemY(int _cx, int _y, int _cz, int _radius, std::uint8_t _plate, std::uint8_t _mark) noexcept
{
  const auto r = static_cast<float>(_radius);
  for (int z = _cz - _radius - 2; z <= _cz + _radius + 2; ++z)
  {
    for (int x = _cx - _radius - 2; x <= _cx + _radius + 2; ++x)
    {
      const float d = HexDistance(static_cast<float>(x - _cx), static_cast<float>(z - _cz));
      if (d > r + 1.0f)
      {
        continue;
      }
      const bool ring = d > r - 1.2f || (d > r * 0.45f - 0.6f && d <= r * 0.45f + 0.6f) || (x == _cx && z <= _cz && d < r * 0.45f);
      Set(x, _y, z, ring ? _mark : _plate);
    }
  }
}

void VoxelBuilder::HexEmblemX(int _x, int _cy, int _cz, int _radius, std::uint8_t _plate, std::uint8_t _mark) noexcept
{
  const auto r = static_cast<float>(_radius);
  for (int z = _cz - _radius - 2; z <= _cz + _radius + 2; ++z)
  {
    for (int y = _cy - _radius - 2; y <= _cy + _radius + 2; ++y)
    {
      const float d = HexDistance(static_cast<float>(y - _cy), static_cast<float>(z - _cz));
      if (d > r + 1.0f)
      {
        continue;
      }
      const bool ring = d > r - 1.2f || (d > r * 0.45f - 0.6f && d <= r * 0.45f + 0.6f) || (y == _cy && z <= _cz && d < r * 0.45f);
      Set(_x, y, z, ring ? _mark : _plate);
    }
  }
}

} // namespace AssetGen
