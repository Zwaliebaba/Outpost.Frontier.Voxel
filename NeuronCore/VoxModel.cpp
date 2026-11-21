#include "VoxModel.h"

#include <algorithm>

namespace Neuron
{

VoxModel::VoxModel(std::uint32_t _sizeX, std::uint32_t _sizeY, std::uint32_t _sizeZ)
  : m_sizeX(_sizeX),
    m_sizeY(_sizeY),
    m_sizeZ(_sizeZ),
    m_cells(static_cast<std::size_t>(_sizeX) * _sizeY * _sizeZ, VOX_EMPTY)
{
}

bool VoxModel::Contains(int _x, int _y, int _z) const noexcept
{
  return _x >= 0 && _y >= 0 && _z >= 0 && static_cast<std::uint32_t>(_x) < m_sizeX && static_cast<std::uint32_t>(_y) < m_sizeY &&
         static_cast<std::uint32_t>(_z) < m_sizeZ;
}

std::size_t VoxModel::Index(int _x, int _y, int _z) const noexcept
{
  return (static_cast<std::size_t>(_z) * m_sizeY + static_cast<std::size_t>(_y)) * m_sizeX + static_cast<std::size_t>(_x);
}

std::uint8_t VoxModel::At(int _x, int _y, int _z) const noexcept
{
  return Contains(_x, _y, _z) ? m_cells[Index(_x, _y, _z)] : VOX_EMPTY;
}

void VoxModel::Set(int _x, int _y, int _z, std::uint8_t _colorIndex) noexcept
{
  if (Contains(_x, _y, _z))
  {
    m_cells[Index(_x, _y, _z)] = _colorIndex;
  }
}

std::size_t VoxModel::CountSolid() const noexcept
{
  return static_cast<std::size_t>(std::ranges::count_if(m_cells, [](std::uint8_t _c) { return _c != VOX_EMPTY; }));
}

} // namespace Neuron
