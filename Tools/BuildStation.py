"""Author the Frontier industrial station kit; committed .vox files remain editable sources.

Run from any directory: python Tools/BuildStation.py [--output Assets/Models]
Requires NumPy for this development tool only. No game/runtime dependencies are added.
The canonical palette is copied unchanged from Frigate.vox. Most component grids double
on each axis and use a 0.75 world-unit voxel in Scene.txt, making them 1.5 times larger.
Ships retain their original one-unit voxels. The drydock and radiator have larger custom grids.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import numpy as np

from VoxelAssets import ReadVox, SurfaceCount, WriteVox

ROOT = Path(__file__).resolve().parent.parent
HULL, DARK, GRAY, LIGHT, WHITE = 5, 82, 12, 16, 20
STEEL, DECK, ORANGE, TEAL = 61, 68, 29, 92
WINDOW, WARM, CYAN, RED = 201, 204, 220, 212
GLYPHS = {
  "0": ("111", "101", "101", "101", "111"),
  "1": ("010", "110", "010", "010", "111"),
  "2": ("111", "001", "111", "100", "111"),
  "3": ("111", "001", "111", "001", "111"),
  "4": ("101", "101", "111", "001", "001"),
  "F": ("111", "100", "110", "100", "100"),
  "H": ("101", "101", "111", "101", "101"),
  "R": ("110", "101", "110", "101", "101"),
  "Y": ("101", "101", "010", "010", "010"),
  "-": ("000", "000", "111", "000", "000"),
}


class Volume:
  def __init__(self, _dimensions):
    self.m_cells = np.zeros(_dimensions, dtype=np.uint8)

  def Box(self, _a, _b, _color):
    start = [max(0, int(round(n))) for n in _a]
    end = [min(size, int(round(n))) for n, size in zip(_b, self.m_cells.shape)]
    if all(a < b for a, b in zip(start, end)):
      self.m_cells[tuple(slice(a, b) for a, b in zip(start, end))] = _color

  def Beam(self, _a, _b, _color=STEEL, _width=2):
    start, end = np.array(_a), np.array(_b)
    count = int(np.max(np.abs(end - start))) * 2 + 1
    for point in np.linspace(start, end, max(1, count)):
      self.Box(np.floor(point - _width / 2), np.floor(point - _width / 2) + _width, _color)

  def Prism(self, _a, _b, _color, _chamfer=4):
    x0, y0, z0 = map(int, _a)
    x1, y1, z1 = map(int, _b)
    for x in range(x0, x1):
      inset = max(0, _chamfer - min(x - x0, x1 - 1 - x))
      self.Box((x, y0 + inset, z0), (x + 1, y1 - inset, z1), _color)

  def Cylinder(self, _center, _radius, _start, _end, _color, _axis=2, _inner=0):
    other = [a for a in range(3) if a != _axis]
    center = np.array(_center)
    for first in range(math.floor(center[0] - _radius), math.ceil(center[0] + _radius)):
      for second in range(math.floor(center[1] - _radius), math.ceil(center[1] + _radius)):
        # An octagonal section reads as fabricated pressure-vessel armor rather than a smooth tube.
        delta = np.abs(np.array((first + 0.5, second + 0.5)) - center)
        radius = max(max(delta), sum(delta) / 1.41421356)
        if _inner <= radius < _radius:
          start, end = [0, 0, 0], [0, 0, 0]
          start[_axis], end[_axis] = _start, _end
          for axis, value in zip(other, (first, second)):
            start[axis], end[axis] = value, value + 1
          self.Box(start, end, _color)

  def TopPanel(self, _x0, _y0, _x1, _y1, _z, _color=GRAY):
    self.Box((_x0, _y0, _z), (_x1, _y1, _z + 2), DARK)
    self.Prism((_x0 + 1, _y0 + 1, _z + 1), (_x1 - 1, _y1 - 1, _z + 3), _color, 2)
    for x in (_x0 + 2, _x1 - 3):
      for y in (_y0 + 2, _y1 - 3):
        self.Box((x, y, _z + 3), (x + 1, y + 1, _z + 4), STEEL)

  def FrontPanel(self, _x0, _x1, _y, _z0, _z1, _color=GRAY):
    self.Box((_x0, _y, _z0), (_x1, _y + 3, _z1), STEEL)
    self.Box((_x0 + 1, _y - 1, _z0 + 1), (_x1 - 1, _y + 2, _z1 - 1), _color)

  def Vent(self, _x0, _x1, _y, _z0, _z1):
    self.FrontPanel(_x0, _x1, _y, _z0, _z1, HULL)
    for z in range(_z0 + 2, _z1 - 1, 3):
      self.Box((_x0 + 2, _y - 2, z), (_x1 - 2, _y, z + 1), STEEL)

  def Windows(self, _x0, _x1, _y, _z, _pitch=5):
    self.Box((_x0 - 1, _y, _z - 1), (_x1 + 1, _y + 2, _z + 3), DARK)
    for x in range(_x0, _x1 - 1, _pitch):
      color = WINDOW if (x // _pitch) % 5 != 2 else 74
      self.Box((x, _y - 1, _z), (x + 2, _y + 1, _z + 1), color)

  def Text(self, _text, _x, _y, _z, _color=WHITE, _scale=1):
    for letter in _text:
      for row, pattern in enumerate(GLYPHS[letter]):
        for column, value in enumerate(pattern):
          if value == "1":
            x, z = _x + column * _scale, _z + (4 - row) * _scale
            self.Box((x, _y, z), (x + _scale, _y + 1, z + _scale), _color)
      _x += 4 * _scale

  def RailX(self, _x0, _x1, _y, _z):
    self.Box((_x0, _y, _z + 4), (_x1, _y + 1, _z + 5), STEEL)
    for x in range(_x0, _x1, 8):
      self.Box((x, _y, _z), (x + 1, _y + 1, _z + 5), STEEL)

  def RailY(self, _x, _y0, _y1, _z):
    self.Box((_x, _y0, _z + 4), (_x + 1, _y1, _z + 5), STEEL)
    for y in range(_y0, _y1, 8):
      self.Box((_x, y, _z), (_x + 1, y + 1, _z + 5), STEEL)

  def WallPanel(self, _axis, _plane, _a, _b, _direction=1, _vent=False):
    """A bolted, recessed equipment cassette on any exterior face, including the underside."""
    other = [axis for axis in range(3) if axis != _axis]

    def Layer(_depth, _low, _high, _color):
      start, end = [0, 0, 0], [0, 0, 0]
      start[_axis] = _plane + _depth * _direction
      end[_axis] = start[_axis] + 1
      for axis, a, b in zip(other, _low, _high):
        start[axis], end[axis] = a, b
      self.Box(start, end, _color)

    Layer(0, _a, _b, STEEL)
    Layer(1, (_a[0] + 1, _a[1] + 1), (_b[0] - 1, _b[1] - 1), DARK if _vent else GRAY)
    if _vent:
      for v in range(_a[1] + 3, _b[1] - 2, 3):
        Layer(2, (_a[0] + 3, v), (_b[0] - 3, v + 1), GRAY)
    else:
      Layer(2, (_a[0] + 3, _a[1] + 2), (_a[0] + 4, _b[1] - 2), LIGHT)
      Layer(2, (_b[0] - 6, _a[1] + 3), (_b[0] - 3, _a[1] + 4), ORANGE)
    for u in (_a[0] + 2, _b[0] - 3):
      for v in (_a[1] + 2, _b[1] - 3):
        Layer(2, (u, v), (u + 1, v + 1), STEEL)


def FinishExterior(_volume, _name):
  # Service cassettes continue the material language around back, side and bottom faces.
  # The authoring coordinates deliberately follow each real hull face; no floating greeble scatter.
  profiles = {
    "HangarBay": [(1, 102, (23, 18), (49, 51), 1), (1, 102, (54, 18), (76, 51), 1),
                  (1, 102, (81, 18), (105, 51), 1), (0, 5, (24, 21), (45, 50), -1),
                  (0, 5, (53, 21), (75, 50), -1), (0, 5, (81, 21), (95, 50), -1),
                  (0, 122, (23, 23), (42, 50), 1), (0, 122, (79, 23), (96, 50), 1),
                  (2, 2, (24, 29), (57, 49), -1), (2, 2, (71, 29), (104, 49), -1)],
    "HangarSection": [(1, 68, (12, 14), (34, 44), 1), (1, 68, (39, 14), (61, 44), 1),
                      (1, 68, (66, 14), (84, 44), 1), (0, 3, (13, 17), (23, 43), -1),
                      (0, 3, (50, 17), (62, 43), -1), (0, 92, (13, 17), (23, 43), 1),
                      (0, 92, (50, 17), (62, 43), 1)],
    "CommandBlock": [(1, 68, (18, 12), (40, 34), 1), (1, 68, (47, 12), (77, 34), 1),
                     (1, 68, (27, 45), (68, 57), 1), (0, 10, (23, 14), (60, 32), -1),
                     (0, 85, (23, 14), (60, 32), 1)],
    "Habitat": [(1, 71, (18, 12), (41, 35), 1), (1, 71, (48, 12), (71, 35), 1),
                (1, 71, (78, 12), (101, 35), 1), (0, 9, (26, 15), (58, 34), -1),
                (0, 110, (26, 15), (58, 34), 1)],
    "Refinery": [(1, 76, (16, 14), (49, 34), 1), (0, 6, (24, 15), (59, 34), -1)],
    "TurretPlatform": [(1, 81, (33, 24), (58, 36), 1)],
    "Yard": [(1, 236, (21, 16), (51, 32), 1), (1, 236, (58, 16), (88, 32), 1),
             (1, 236, (96, 16), (126, 32), 1), (1, 236, (133, 16), (159, 32), 1)],
  }
  for index, profile in enumerate(profiles.get(_name, [])):
    _volume.WallPanel(*profile, _vent=index % 3 != 1)
  return _volume


def HangarBay():
  volume = Volume((128, 104, 76))
  volume.Prism((0, 0, 2), (128, 104, 10), HULL, 6)
  volume.Prism((2, 2, 8), (126, 102, 11), DECK, 5)
  volume.Box((5, 18, 10), (20, 104, 66), HULL)
  volume.Box((108, 18, 10), (123, 104, 66), HULL)
  volume.Box((18, 96, 10), (110, 103, 68), HULL)
  volume.Box((10, 14, 62), (118, 100, 69), HULL)
  # Armored portal: a deep opening with chamfered upper corners and layered ribs.
  for y in (10, 36, 62, 88):
    for x in (10, 111):
      volume.Box((x, y, 9), (x + 7, y + 5, 62), GRAY)
      volume.Box((x + 2, y - 1, 12), (x + 5, y, 52), WHITE)
    volume.Box((10, y, 61), (118, y + 5, 66), STEEL)
    volume.Beam((17, y + 2, 52), (28, y + 2, 64), STEEL, 3)
    volume.Beam((111, y + 2, 52), (100, y + 2, 64), STEEL, 3)
    for x in (24, 99):
      volume.Box((x, y + 1, 60), (x + 5, y + 3, 62), WARM)
  # Fine chevrons on entrance columns, not wall-size stripes.
  for x in (10, 115):
    for z in range(14, 54):
      volume.Box((x, 8, z), (x + 3, 10, z + 1), 35 if (z + x) % 8 < 4 else 38)
  volume.FrontPanel(17, 111, 8, 62, 72, GRAY)
  volume.Box((25, 6, 63), (103, 8, 65), ORANGE)
  volume.Text("F-01", 53, 6, 66)
  volume.Text("01", 4, 10, 54)
  # Small booths and service balconies, grounded machinery, and a back-wall blast door.
  for x in (20, 96):
    volume.Box((x, 61, 12), (x + 12, 94, 28), GRAY)
    volume.Windows(x + 2, x + 10, 60, 24, 3)
    volume.Box((x - 1, 32, 29), (x + 14, 94, 31), STEEL)
    volume.RailY(x + (14 if x < 64 else -1), 32, 94, 31)
    for y in (30, 54, 78):
      volume.Box((x + 3, y, 11), (x + 9, y + 9, 16), ORANGE)
      volume.TopPanel(x + 3, y, x + 9, y + 9, 16, GRAY)
  volume.FrontPanel(45, 83, 94, 12, 48, STEEL)
  for x in range(48, 81, 5):
    volume.Box((x, 92, 14), (x + 1, 94, 45), DARK)
  volume.Box((62, 91, 14), (64, 94, 44), ORANGE)
  # Dock deck markings are interrupted by access panels and drain gratings.
  for x in (36, 89):
    volume.Box((x, 2, 11), (x + 1, 89, 12), WHITE)
    for y in range(8, 89, 12):
      volume.Box((x + 2, y, 11), (x + 3, y + 3, 12), CYAN)
  for y in (26, 54, 82):
    volume.Box((47, y, 11), (81, y + 1, 12), 70)
    for x in range(50, 79, 3):
      volume.Box((x, y + 2, 11), (x + 1, y + 5, 12), DARK)
  for x in (3, 121):
    for y in (18, 44, 70):
      volume.TopPanel(x, y, x + 5, y + 20, 66, WHITE)
  # Unequal armor islands frame actual ventilation machinery, not a checkerboard roof.
  for x0, y0, x1, y1, color in ((22, 20, 38, 43, LIGHT), (22, 49, 38, 92, GRAY),
                               (44, 78, 79, 94, LIGHT), (88, 20, 106, 66, LIGHT),
                               (87, 73, 106, 94, GRAY)):
    volume.TopPanel(x0, y0, x1, y1, 69, color)
  for x in (54, 77):
    volume.Cylinder((x, 42), 10, 69, 72, DARK)
    volume.Cylinder((x, 42), 10, 72, 74, STEEL, _inner=8)
    for a, b in (((x - 6, 36, 72), (x + 6, 48, 72)), ((x + 6, 36, 72), (x - 6, 48, 72))):
      volume.Beam(a, b, GRAY, 1)
    volume.Cylinder((x, 42), 2, 72, 74, LIGHT)
  volume.Box((47, 59, 69), (82, 70, 72), DARK)
  for x in range(49, 81, 4):
    volume.Box((x, 60, 72), (x + 1, 69, 74), STEEL)
  volume.Box((40, 20, 70), (43, 91, 73), HULL)
  volume.Box((41, 21, 73), (42, 90, 74), ORANGE)
  for y in (26, 56, 84):
    volume.Box((39, y, 70), (44, y + 2, 74), STEEL)
  volume.RailX(46, 80, 97, 69)
  volume.Box((5, 100, 18), (123, 104, 22), ORANGE)
  volume.Box((125, 48, 22), (128, 72, 47), STEEL)
  volume.Box((126, 51, 26), (128, 69, 43), DARK)
  return volume


def HangarSection():
  volume = Volume((96, 72, 60))
  volume.Prism((0, 0, 2), (96, 72, 9), STEEL, 4)
  volume.Prism((3, 5, 9), (93, 69, 50), HULL, 5)
  volume.Prism((5, 6, 50), (91, 68, 55), GRAY, 5)
  for x in (6, 28, 50, 72, 87):
    volume.Box((x, 3, 9), (x + 3, 69, 55), STEEL)
    volume.Box((x, 5, 53), (x + 3, 67, 56), LIGHT)
    volume.Box((x, 2, 11), (x + 3, 4, 14), ORANGE)
  for x in (10, 32, 54):
    volume.FrontPanel(x, x + 17, 4, 16, 38, GRAY)
    volume.Windows(x + 2, x + 15, 2, 42, 4)
    volume.Vent(x + 2, x + 15, 1, 21, 32)
  volume.FrontPanel(76, 86, 2, 15, 39, TEAL)
  volume.Box((80, 0, 19), (81, 2, 34), CYAN)
  volume.Text("F-02", 34, 2, 10)
  for x in (10, 32, 54, 76):
    volume.TopPanel(x, 13, x + 16, 37, 54, LIGHT)
    volume.TopPanel(x, 43, x + 16, 63, 54, GRAY)
    for y in range(46, 61, 4):
      volume.Box((x + 3, y, 57), (x + 13, y + 1, 58), DARK)
  for x in (0, 92):
    volume.Box((x, 25, 18), (x + 4, 47, 42), STEEL)
    volume.Box((x, 28, 22), (x + 4, 44, 38), DARK)
  volume.Box((7, 0, 6), (89, 6, 8), DECK)
  volume.RailX(8, 88, 0, 8)
  return volume


def CommandBlock():
  volume = Volume((96, 80, 92))
  volume.Prism((5, 6, 1), (91, 74, 9), HULL, 8)
  volume.Prism((10, 12, 9), (86, 69, 39), GRAY, 5)
  volume.Prism((3, 7, 37), (93, 75, 42), STEEL, 9)
  volume.Prism((17, 16, 42), (79, 69, 61), HULL, 4)
  volume.Prism((12, 9, 60), (84, 72, 66), LIGHT, 8)
  volume.Prism((22, 20, 66), (74, 65, 78), HULL, 5)
  volume.TopPanel(22, 22, 74, 64, 78, GRAY)
  # Stacked narrow operations windows, recessed bays, buttresses and perimeter catwalks.
  for z in (19, 29):
    volume.Windows(16, 80, 11, z, 5)
  volume.Windows(22, 74, 15, 52, 4)
  volume.Windows(27, 69, 19, 71, 4)
  for x in (11, 30, 62, 82):
    volume.Box((x, 9, 9), (x + 3, 14, 37), LIGHT)
    volume.Box((x, 7, 10), (x + 3, 10, 13), ORANGE)
  volume.FrontPanel(38, 58, 8, 10, 28, TEAL)
  volume.Text("01", 43, 6, 15)
  volume.RailX(14, 82, 7, 42)
  volume.Box((18, 8, 61), (78, 10, 63), ORANGE)
  for x in (15, 76):
    volume.Vent(x, x + 9, 13, 43, 58)
    volume.TopPanel(x, 46, x + 8, 65, 66, GRAY)
  for x, height in ((27, 90), (66, 87)):
    volume.Box((x - 2, 40, 80), (x + 3, 45, 83), STEEL)
    volume.Box((x, 42, 83), (x + 1, 43, height), LIGHT)
    volume.Box((x, 42, height), (x + 1, 43, height + 1), RED)
  volume.Cylinder((48, 50), 8, 81, 84, STEEL)
  for x in (14, 81):
    volume.Box((x, 23, 18), (x + 2, 64, 21), ORANGE)
  return volume


def TankCluster():
  volume = Volume((56, 32, 68))
  volume.Prism((0, 0, 1), (56, 32, 8), HULL, 3)
  for x in (14, 42):
    volume.Cylinder((x, 16), 12, 11, 56, WHITE)
    for z, radius in ((8, 8), (9, 10), (56, 11), (58, 10), (60, 7), (62, 4)):
      volume.Cylinder((x, 16), radius, z, z + 3, GRAY)
    for z in (16, 39, 52):
      volume.Cylinder((x, 16), 13, z, z + 3, STEEL)
    volume.Cylinder((x, 16), 12, 27, 32, ORANGE)
    volume.Cylinder((x, 16), 3, 64, 67, STEEL)
    for z in range(12, 53, 5):
      volume.Box((x + 9, 4, z), (x + 11, 7, z + 1), STEEL)
    volume.Box((x - 3, 3, 42), (x + 4, 5, 50), DARK)
    volume.Text("1" if x == 14 else "2", x - 1, 2, 43)
  volume.Box((4, 1, 7), (52, 3, 10), ORANGE)
  for x in (4, 50):
    volume.Box((x, 2, 8), (x + 2, 4, 27), STEEL)
  volume.Box((11, 27, 35), (45, 30, 38), ORANGE)
  volume.Box((27, 27, 12), (30, 30, 37), STEEL)
  return volume


def DockingCollar():
  volume = Volume((36, 24, 36))
  volume.Cylinder((18, 18), 16, 6, 24, HULL, 1, 11)
  for y in (3, 6, 18):
    volume.Cylinder((18, 18), 17, y, y + 3, STEEL, 1, 11)
  volume.Cylinder((18, 18), 15, 1, 4, LIGHT, 1, 12)
  volume.Cylinder((18, 18), 13, 5, 7, ORANGE, 1, 11)
  for x in (5, 27):
    for z in (6, 26):
      volume.Box((x, 0, z), (x + 4, 5, z + 4), GRAY)
      volume.Box((x + 1, 0, z + 1), (x + 2, 1, z + 2), WARM)
  for x in (0, 32):
    volume.Box((x, 8, 14), (x + 4, 21, 22), GRAY)
  volume.Box((16, 1, 31), (20, 3, 32), CYAN)
  return volume


def TrussBeam():
  volume = Volume((192, 16, 16))
  for y in (1, 13):
    for z in (1, 13):
      volume.Box((0, y, z), (192, y + 2, z + 2), STEEL)
  for x in range(0, 192, 24):
    volume.Box((x, 0, 0), (x + 3, 16, 16), GRAY)
    volume.Box((x, 3, 3), (x + 3, 13, 13), 0)
    for y in (1, 14):
      volume.Beam((x + 2, y, 2), (x + 23, y, 14), ORANGE, 1)
      volume.Beam((x + 2, y, 14), (x + 23, y, 2), STEEL, 1)
    volume.Beam((x + 2, 2, 14), (x + 23, 14, 14), STEEL, 1)
    volume.Box((x + 9, 5, 6), (x + 21, 11, 9), HULL)
    volume.Box((x + 11, 4, 7), (x + 14, 5, 8), ORANGE)
    volume.Box((x + 2, 0, 12), (x + 3, 1, 13), WARM)
  volume.Box((0, 6, 2), (192, 8, 4), TEAL)
  return volume


def TrussTower():
  volume = Volume((24, 24, 96))
  for x in (2, 20):
    for y in (2, 20):
      volume.Box((x, y, 0), (x + 2, y + 2, 91), STEEL)
  for z in range(2, 84, 16):
    volume.Box((1, 1, z), (23, 23, z + 2), STEEL)
    volume.Box((4, 4, z), (20, 20, z + 2), 0)
    for y in (3, 21):
      volume.Beam((3, y, z + 2), (21, y, z + 15), STEEL, 1)
    for x in (3, 21):
      volume.Beam((x, 3, z + 2), (x, 21, z + 15), GRAY, 1)
    volume.Box((0, 0, z), (3, 3, z + 1), WARM)
  volume.Box((9, 10, 0), (14, 14, 85), HULL)
  for z in range(3, 83, 4):
    volume.Box((15, 8, z), (19, 9, z + 1), ORANGE)
  volume.Prism((0, 0, 84), (24, 24, 89), GRAY, 3)
  volume.Box((5, 4, 89), (19, 20, 91), TEAL)
  volume.Box((11, 11, 91), (13, 13, 95), LIGHT)
  volume.Box((11, 11, 95), (12, 12, 96), RED)
  return volume


def Habitat():
  volume = Volume((120, 84, 48))
  volume.Prism((2, 6, 2), (118, 78, 9), HULL, 7)
  volume.Prism((9, 15, 9), (111, 72, 39), GRAY, 8)
  volume.Prism((12, 18, 39), (108, 69, 43), WHITE, 8)
  for z in (15, 25, 34):
    volume.Windows(18, 102, 14, z, 5)
    volume.Box((14, 14, z - 3), (106, 16, z - 2), TEAL)
  for x in (10, 34, 58, 82, 106):
    volume.Box((x, 11, 9), (x + 3, 73, 41), LIGHT)
    volume.Box((x, 9, 8), (x + 3, 14, 13), ORANGE)
  for x in (18, 42, 66, 90):
    volume.TopPanel(x, 24, x + 17, 48, 42, LIGHT)
    volume.TopPanel(x, 52, x + 17, 65, 42, TEAL)
  volume.Box((48, 4, 9), (72, 16, 24), HULL)
  volume.FrontPanel(54, 66, 3, 10, 22, TEAL)
  volume.Box((59, 1, 12), (60, 3, 21), WARM)
  volume.Text("H-01", 79, 10, 8)
  volume.RailX(12, 108, 5, 9)
  for x in (4, 112):
    volume.Box((x, 31, 15), (x + 4, 59, 29), STEEL)
    volume.Box((x, 34, 18), (x + 4, 56, 26), DARK)
  return volume


def Reactor():
  volume = Volume((102, 84, 78))
  volume.Prism((0, 2, 1), (102, 82, 9), HULL, 7)
  volume.Prism((25, 15, 8), (77, 71, 23), GRAY, 6)
  volume.Cylinder((51, 43), 22, 21, 63, HULL)
  for z in (24, 42, 59):
    volume.Cylinder((51, 43), 24, z, z + 4, STEEL)
  volume.Cylinder((51, 43), 22, 44, 47, TEAL)
  for x in (32, 67):
    volume.Box((x, 24, 26), (x + 3, 29, 58), LIGHT)
    volume.Box((x + 1, 23, 29), (x + 2, 24, 39), CYAN)
  volume.Cylinder((51, 43), 18, 63, 67, GRAY)
  volume.Cylinder((51, 43), 13, 67, 70, STEEL)
  volume.Cylinder((51, 43), 7, 70, 73, TEAL)
  # Heat exchangers: real negative space between individual graphite radiator leaves.
  for x in (3, 80):
    volume.Box((x + 6, 10, 8), (x + 11, 75, 16), STEEL)
    for y in range(12, 73, 7):
      volume.Box((x, y, 14), (x + 19, y + 2, 57), HULL)
      volume.Box((x + 1, y, 19), (x + 18, y + 1, 54), TEAL)
      volume.Box((x, y, 55), (x + 19, y + 2, 58), STEEL)
    volume.Box((x + 8, 12, 59), (x + 10, 74, 61), ORANGE)
    volume.Box((x + 8, 12, 57), (x + 10, 14, 61), ORANGE)
    volume.Box((x + 8, 72, 57), (x + 10, 74, 61), ORANGE)
    volume.Beam((x + 8, 42, 26), (51, 42, 26), STEEL, 3)
  volume.Vent(32, 70, 13, 11, 22)
  volume.Text("R-01", 43, 11, 13)
  return volume


def Refinery():
  volume = Volume((108, 86, 82))
  volume.Prism((1, 1, 1), (107, 85, 10), HULL, 6)
  volume.Prism((6, 10, 10), (62, 77, 40), GRAY, 5)
  for x in (10, 30, 50):
    volume.FrontPanel(x, x + 13, 9, 15, 31, HULL)
    volume.Vent(x + 1, x + 12, 7, 17, 29)
    volume.TopPanel(x, 17, x + 12, 43, 40, LIGHT)
  volume.Windows(12, 58, 8, 34, 5)
  # Distillation columns, collars, pipe elbows, valves and a common service bridge.
  for x, y, height in ((77, 22, 69), (89, 57, 78), (63, 61, 60)):
    volume.Cylinder((x, y), 9, 10, height - 6, LIGHT)
    for z in range(18, height - 6, 14):
      volume.Cylinder((x, y), 10, z, z + 2, STEEL)
    volume.Cylinder((x, y), 7, height - 6, height - 2, GRAY)
    volume.Cylinder((x, y), 4, height - 2, height, ORANGE)
    volume.Box((x - 1, y - 10, 13), (x + 1, y - 8, height - 4), ORANGE)
    for z in range(15, height - 5, 4):
      volume.Box((x + 5, y - 8, z), (x + 8, y - 7, z + 1), STEEL)
    volume.Beam((x, y, 12), (x, 70, 12), STEEL, 3)
    volume.Beam((x, 70, 12), (45, 70, 12), STEEL, 3)
  volume.Box((8, 47, 40), (98, 52, 42), DECK)
  volume.RailX(8, 98, 47, 42)
  volume.Box((7, 17, 42), (62, 20, 46), ORANGE)
  for x in range(12, 60, 12):
    volume.Box((x, 18, 41), (x + 2, 20, 46), STEEL)
  return volume


def TurretPlatform():
  volume = Volume((92, 100, 62))
  volume.Prism((3, 16, 2), (89, 96, 10), HULL, 15)
  volume.Cylinder((46, 57), 25, 10, 17, STEEL)
  volume.Cylinder((46, 57), 21, 17, 21, ORANGE)
  volume.Prism((22, 31, 21), (70, 82, 41), GRAY, 10)
  volume.Prism((26, 38, 41), (66, 77, 46), LIGHT, 6)
  for x in (27, 58):
    volume.Box((x, 6, 27), (x + 7, 51, 34), HULL)
    volume.Box((x - 1, 4, 26), (x + 8, 12, 35), STEEL)
    volume.Box((x + 1, 3, 28), (x + 6, 5, 32), DARK)
    for y in (17, 26, 35):
      volume.Box((x - 1, y, 26), (x + 8, y + 2, 35), GRAY)
  volume.Vent(32, 60, 32, 35, 41)
  volume.Cylinder((46, 59), 6, 47, 50, TEAL)
  volume.Box((45, 58, 50), (47, 60, 59), STEEL)
  volume.Box((45, 58, 59), (46, 59, 60), RED)
  for x in (12, 76):
    volume.TopPanel(x, 52, x + 6, 77, 10, TEAL)
    volume.Box((x, 30, 10), (x + 4, 32, 12), CYAN)
  return volume


def ShieldGenerator():
  volume = Volume((104, 84, 92))
  volume.Prism((1, 6, 1), (103, 78, 10), HULL, 9)
  volume.Prism((20, 19, 10), (84, 69, 28), GRAY, 8)
  volume.Cylinder((52, 43), 18, 28, 35, STEEL)
  volume.Cylinder((52, 43), 13, 35, 45, TEAL)
  volume.Box((47, 38, 44), (57, 48, 69), STEEL)
  # Faceted concave antenna, looking out of the station's front face (-VOX Y).
  for y in range(17, 34):
    radius = 31 - (y - 17)
    volume.Cylinder((52, 60), radius, y, y + 1, LIGHT, 1, max(0, radius - 2))
  volume.Cylinder((52, 60), 13, 32, 35, GRAY, 1)
  for a, b in (((25, 18, 60), (52, 3, 60)), ((79, 18, 60), (52, 3, 60)),
               ((52, 18, 86), (52, 3, 60))):
    volume.Beam(a, b, STEEL, 1)
  volume.Box((50, 1, 58), (54, 6, 62), TEAL)
  volume.Box((51, 0, 59), (53, 1, 61), CYAN)
  for x in (7, 87):
    volume.TopPanel(x, 24, x + 10, 66, 11, TEAL)
    for y in range(28, 64, 6):
      volume.Box((x, y, 13), (x + 10, y + 1, 15), STEEL)
  volume.Windows(31, 73, 18, 18, 7)
  return volume


def Yard():
  # A deliberately larger open drydock: 135 x 180 x 96 world units, not a scaled-up solid box.
  volume = Volume((180, 240, 128))
  for x in (5, 159):
    volume.Prism((x, 4, 3), (x + 16, 236, 13), HULL, 4)
    volume.Box((x + 3, 12, 12), (x + 13, 230, 17), GRAY)
    volume.Box((x + 6, 14, 17), (x + 8, 228, 18), ORANGE)
    volume.RailY(x + (0 if x == 5 else 15), 16, 228, 17)
    for y in range(24, 225, 16):
      volume.Box((x + 4, y, 18), (x + 6, y + 3, 19), CYAN)
  # Rear plant room bridges the two rails. Interior remains open for capital-ship clearance.
  volume.Prism((6, 204, 12), (174, 237, 37), HULL, 6)
  for x in range(17, 166, 25):
    volume.TopPanel(x, 210, x + 19, 230, 37, GRAY)
    volume.Windows(x + 2, x + 17, 203, 28, 4)
  # Four external lattice portals, each with walkways, teeth, bolted shoes and crane rails.
  for y in (26, 82, 138, 194):
    for x in (8, 162):
      volume.Box((x - 5, y - 7, 13), (x + 15, y + 10, 22), GRAY)
      for dx in (0, 8):
        for dy in (-2, 5):
          volume.Box((x + dx, y + dy, 21), (x + dx + 3, y + dy + 3, 111), STEEL)
      for z in range(25, 109, 14):
        volume.Beam((x + 2, y - 1, z), (x + 10, y - 1, z + 13), ORANGE, 2)
        volume.Box((x - 1, y - 4, z), (x + 13, y + 10, z + 2), GRAY)
      volume.Box((x - 3, y - 5, 110), (x + 14, y + 11, 118), GRAY)
      volume.Box((x - 1, y - 6, 112), (x + 12, y - 5, 114), ORANGE)
      volume.Box((x + 5, y + 2, 118), (x + 6, y + 3, 124), STEEL)
      volume.Box((x + 5, y + 2, 124), (x + 6, y + 3, 125), RED)
    for z in (105, 114):
      volume.Box((18, y - 2, z), (164, y + 1, z + 3), STEEL)
    for x in range(22, 161, 14):
      volume.Beam((x, y - 1, 107), (x + 13, y - 1, 115), ORANGE, 1)
    volume.Box((19, y + 2, 104), (163, y + 7, 106), DECK)
    volume.RailX(20, 162, y + 7, 106)
    for x in (26, 146):
      volume.Box((x, y - 2, 102), (x + 7, y + 2, 104), WARM)
  # Longitudinal upper rails and travelling hoist, suspended with actual voxel cables.
  for x in (15, 162):
    volume.Box((x, 24, 114), (x + 3, 202, 118), STEEL)
  volume.Box((34, 79, 116), (146, 88, 120), ORANGE)
  volume.Box((79, 77, 110), (101, 91, 119), HULL)
  for x in (83, 96):
    volume.Box((x, 82, 67), (x + 1, 83, 112), STEEL)
  volume.Box((81, 79, 64), (100, 86, 69), ORANGE)
  volume.Box((86, 79, 62), (95, 86, 64), GRAY)
  # Repair manipulators and decks inside both rails.
  for y in (54, 110, 166):
    for x, end in ((20, 47), (159, 133)):
      volume.Box((min(x, end), y, 28), (max(x, end) + 1, y + 5, 32), STEEL)
      volume.Box((end - 2, y - 1, 23), (end + 3, y + 6, 34), ORANGE)
      volume.Box((x - 2, y, 13), (x + 3, y + 5, 30), GRAY)
  volume.Text("Y-01", 73, 202, 16, WHITE, 2)
  return volume


def RadiatorArray():
  volume = Volume((224, 128, 24))
  # Open folding banks on either side of a deep service keel. These are non-emissive
  # heat-rejection panels, so large surfaces cannot wash out the station's night side.
  for x in (104, 117):
    volume.Box((x, 0, 2), (x + 3, 128, 10), STEEL)
  for y in range(4, 125, 20):
    volume.Box((101, y, 2), (123, y + 3, 12), GRAY)
    volume.Beam((105, y + 2, 3), (119, y + 19, 8), ORANGE, 1)
  for x0, x1 in ((4, 100), (124, 220)):
    for y0 in (8, 37, 66, 95):
      volume.Box((x0, y0, 11), (x1, y0 + 25, 14), HULL)
      volume.Box((x0, y0, 14), (x1, y0 + 1, 17), LIGHT)
      volume.Box((x0, y0 + 24, 14), (x1, y0 + 25, 17), STEEL)
      volume.Box((x0, y0, 14), (x0 + 1, y0 + 25, 17), STEEL)
      volume.Box((x1 - 1, y0, 14), (x1, y0 + 25, 17), STEEL)
      for x in range(x0 + 3, x1 - 3, 12):
        volume.Box((x, y0 + 3, 14), (x + 9, y0 + 22, 16), TEAL)
        for y in range(y0 + 4, y0 + 22, 3):
          volume.Box((x + 1, y, 16), (x + 8, y + 1, 17), 89)
      volume.Beam((x0, y0 + 1, 10), (x1, y0 + 24, 10), STEEL, 2)
      volume.Box((99 if x0 == 4 else 120, y0 + 9, 8),
                 (105 if x0 == 4 else 126, y0 + 15, 16), ORANGE)
  volume.Box((108, 53, 1), (116, 75, 13), HULL)
  for y in (12, 111):
    volume.Box((110, y, 12), (114, y + 4, 18), GRAY)
    volume.Box((111, y + 1, 18), (112, y + 2, 19), RED)
  return volume


BUILDERS = (HangarBay, HangarSection, CommandBlock, TankCluster, DockingCollar, TrussBeam,
            TrussTower, Habitat, Reactor, Refinery, TurretPlatform, ShieldGenerator, Yard, RadiatorArray)


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--output", type=Path, default=ROOT / "Assets" / "Models")
  options = parser.parse_args()
  options.output.mkdir(parents=True, exist_ok=True)
  _, palette = ReadVox(ROOT / "Assets" / "Models" / "Frigate.vox")
  for builder in BUILDERS:
    cells = FinishExterior(builder(), builder.__name__).m_cells
    path = options.output / f"{builder.__name__}.vox"
    WriteVox(path, cells, palette)
    print(f"{path.stem:18} {str(cells.shape):16} {np.count_nonzero(cells):8,} solid; "
          f"{SurfaceCount(cells):7,} surface")


if __name__ == "__main__":
  Main()
