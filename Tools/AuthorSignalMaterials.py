"""Add canonical yellow strobes and recessed blue glazing to editable VOX source assets.

Run once on existing assets; already-detailed models are preserved on subsequent runs.
Use --source /path/to/baseline/Assets --assets /tmp/Signals for separate reproducible authoring.
No dimensions, pivots, flight definitions or scene placements change.
"""

import argparse
from collections import deque
from pathlib import Path

import numpy as np

from VoxelAssets import ReadVox, WriteVox, STROBE_YELLOW, WINDOW_BLUE
from HullAssets import Flood
from PrepareHulls import REGIONS

ROOT = Path(__file__).resolve().parent.parent
VESSELS = ("Frigate", "EscortLancer", "EscortTalon", "Tug", "Raider")
NATURAL = {"AsteroidMajor", "AsteroidSmallA", "AsteroidSmallB", "AsteroidSmallC",
           "DebrisA", "DebrisB", "DebrisC"}


def PaneNormals(_source, _name):
  # Only the original exterior window paint defines glazing, not sensor-colored internals.
  x, y, z = np.indices(_source.shape)
  window = ((_source >= 73) & (_source <= 76)) | (
    (_source >= 201) & (_source <= 203) & ((x // 5) % 2 == 0))
  if _name == "Frigate":
    window = (_source >= 201) & (_source <= 203) & (y >= 65) & (z >= 30)
    directions = ((0, -1), (0, 1))
  elif _name in ("EscortLancer", "EscortTalon", "Raider"):
    directions = ((2, 1),)
  elif _name == "Tug":
    directions = ((1, 1),)
  else:
    directions = ((1, -1),)
  panes = []
  for axis, side in directions:
    neighbors = np.roll(_source, -side, axis=axis)
    edge = [slice(None)] * 3
    edge[axis] = -1 if side == 1 else 0
    neighbors[tuple(edge)] = 0
    for point in np.argwhere(window & (neighbors == 0)):
      panes.append((tuple(point.tolist()), axis, side))
  return panes


def Detail(_source, _reference, _name, _beacons=None):
  cells = _source.copy()
  # Preserve artists' follow-up edits; the script is an initial pass, never a build step.
  if np.any(cells == STROBE_YELLOW):
    return cells, []
  panes = PaneNormals(_reference, _name)
  coreBounds = next(((low, high) for label, function, low, high in REGIONS.get(_name, [])
                    if function == "core"), None)
  for point, axis, side in panes:
    if cells[point] == 0:
      continue
    # A thin outer pane with a real inspection recess behind it, not glass painted on armor.
    for depth in range(1, 4):
      inside = list(point)
      inside[axis] -= side * depth
      if all(0 <= value < size for value, size in zip(inside, cells.shape)):
        if coreBounds and all(a <= value < b for value, a, b in zip(inside, *coreBounds)):
          continue
        cells[tuple(inside)] = 0
  for point, axis, side in panes:
    if _source[point] != 0:
      cells[point] = WINDOW_BLUE
  if _beacons is None:
    # Top-facing fittings, distributed across the existing footprint. Recolor existing solid
    # cells rather than extending a grid or creating floating lights at bounding-box corners.
    above = np.roll(cells, -1, axis=2)
    above[:, :, -1] = 0
    candidates = np.argwhere((cells != 0) & (above == 0))
    low, high = np.argwhere(cells != 0).min(0), np.argwhere(cells != 0).max(0)
    span = np.maximum(high - low, 1)
    beacons = []
    targets = ((.18, .2), (.82, .8)) if max(cells.shape) < 70 else (
      (.18, .2), (.82, .2), (.18, .8), (.82, .8))
    for a, b in targets:
      target = low + np.array((a, b, .9)) * span
      score = np.sum(((candidates - target) / span) ** 2, axis=1)
      beacons.append(tuple(candidates[int(np.argmin(score))].tolist()))
  else:
    beacons = _beacons
  for point in beacons:
    if cells[point] != 0:
      cells[point] = STROBE_YELLOW
  return cells, beacons


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--source", type=Path, default=ROOT / "Assets")
  parser.add_argument("--assets", type=Path, default=ROOT / "Assets")
  options = parser.parse_args()
  models = options.source / "Models"
  loaded = {path.stem: ReadVox(path) for path in sorted(models.glob("*.vox"))}
  palette = bytearray(loaded["Frigate"][1])
  palette[(STROBE_YELLOW - 1) * 4:STROBE_YELLOW * 4] = bytes((255, 224, 48, 255))
  palette[(WINDOW_BLUE - 1) * 4:WINDOW_BLUE * 4] = bytes((75, 162, 235, 80))
  output = options.assets / "Models"
  output.mkdir(parents=True, exist_ok=True)
  done = set()
  for name in VESSELS:
    reference = loaded[name][0]
    intact, beacons = Detail(loaded[name + "Intact"][0], reference, name)
    legacy, _ = Detail(reference, reference, name, beacons)
    # Preserve original structural ties around thin cockpits. A recess may not strand a mast,
    # roof or canopy. Restore only original occupied cells along the nearest supporting path.
    original = loaded[name + "Intact"][0]
    core = next(low for label, function, low, high in REGIONS[name] if function == "core")
    connected = Flood(intact != 0, core)
    while np.any((intact != 0) & ~connected):
      start = tuple(np.argwhere((intact != 0) & ~connected)[0].tolist())
      previous = {start: None}
      pending = deque([start])
      end = None
      while pending and end is None:
        point = pending.popleft()
        for axis in range(3):
          for side in (-1, 1):
            neighbor = list(point)
            neighbor[axis] += side
            neighbor = tuple(neighbor)
            if not all(0 <= value < size for value, size in zip(neighbor, intact.shape)):
              continue
            if original[neighbor] == 0 or neighbor in previous:
              continue
            previous[neighbor] = point
            if connected[neighbor]:
              end = neighbor
              break
            pending.append(neighbor)
          if end is not None:
            break
      if end is None:
        raise ValueError(f"Original hull was not core-connected: {name}")
      while end is not None:
        if intact[end] == 0:
          intact[end] = original[end]
          if reference[end] != 0:
            legacy[end] = reference[end]
        end = previous[end]
      connected = Flood(intact != 0, core)
    for key, cells in ((name, legacy), (name + "Intact", intact)):
      WriteVox(output / (key + ".vox"), cells, bytes(palette))
      done.add(key)
  for name, (source, _) in loaded.items():
    if name in done:
      continue
    cells = source if name in NATURAL else Detail(source, source, name)[0]
    WriteVox(output / (name + ".vox"), cells, bytes(palette))
  for path in sorted(output.glob("*.vox")):
    cells, _ = ReadVox(path)
    print(f"{path.stem}: {np.count_nonzero(cells == STROBE_YELLOW)} strobe cells, "
          f"{np.count_nonzero(cells == WINDOW_BLUE)} glazing cells")


if __name__ == "__main__":
  Main()
