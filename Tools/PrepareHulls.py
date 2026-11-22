"""Author intact MVP hull masters and derive the F-317 initial-damage fixture.

python Tools/PrepareHulls.py --output /tmp/HullAssets       # reproducible authoring, separate output
python Tools/PrepareHulls.py --refresh                     # rehash edited masters and derive sidecars
Existing scene models are inputs and are never overwritten. Committed VOX masters are editable sources.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from HullAssets import Flatten, Flood, HashFile, HULL_NAMES, ROOT
from VoxelAssets import ReadVox, WriteVox

# Half-open boxes in the existing VOX grids: +Z up, forward +Y. No health thresholds or balance values.
REGIONS = {
  "Frigate": [
    ("Core", "core", (22, 28, 12), (26, 34, 16)),
    ("Reactor", "power", (20, 36, 10), (28, 46, 20)),
    ("ShieldGenerator", "shield", (22, 96, 12), (26, 102, 18)),
    ("EnginePort", "engine", (8, 0, 7), (16, 15, 17)),
    ("EngineStarboard", "engine", (32, 0, 7), (40, 15, 17)),
    ("Sensors", "sensor", (18, 68, 27), (30, 93, 35)),
    ("WeaponFore", "weapon", (20, 110, 25), (28, 127, 28)),
    ("WeaponAft", "weapon", (20, 38, 27), (28, 54, 30)),
    ("Atmospherics", "atmosphere", (28, 60, 11), (34, 88, 23)),
    ("HullSectionB", "structure", (0, 50, 0), (48, 97, 40))],
  "EscortLancer": [
    ("Core", "core", (12, 19, 7), (16, 23, 10)),
    ("Reactor", "power", (12, 10, 7), (16, 16, 10)),
    ("ShieldGenerator", "shield", (12, 24, 7), (16, 28, 10)),
    ("EnginePort", "engine", (3, 6, 3), (8, 31, 10)),
    ("EngineStarboard", "engine", (20, 6, 3), (25, 31, 10)),
    ("Sensors", "sensor", (12, 30, 11), (16, 39, 15)),
    ("WeaponPort", "weapon", (3, 31, 3), (8, 35, 10)),
    ("WeaponStarboard", "weapon", (20, 31, 3), (25, 35, 10))],
  "EscortTalon": [
    ("Core", "core", (20, 18, 6), (24, 22, 9)),
    ("Reactor", "power", (20, 8, 6), (24, 13, 9)),
    ("ShieldGenerator", "shield", (20, 23, 6), (24, 26, 9)),
    ("Engine", "engine", (19, 0, 5), (25, 7, 10)),
    ("Sensors", "sensor", (20, 26, 11), (24, 32, 14)),
    ("WeaponPort", "weapon", (0, 18, 5), (3, 31, 9)),
    ("WeaponStarboard", "weapon", (41, 18, 5), (44, 31, 9)),
    ("WingStrutPort", "structure", (3, 10, 5), (18, 30, 9)),
    ("WingStrutStarboard", "structure", (26, 10, 5), (41, 30, 9))],
  "Tug": [
    ("Core", "core", (11, 16, 6), (15, 20, 10)),
    ("Reactor", "power", (11, 10, 6), (15, 14, 10)),
    ("EnginePort", "engine", (4, 0, 4), (11, 10, 14)),
    ("EngineStarboard", "engine", (15, 0, 4), (22, 10, 14)),
    ("Sensors", "sensor", (9, 27, 15), (17, 35, 19)),
    ("ClampPort", "clamp", (2, 30, 5), (7, 38, 12)),
    ("ClampStarboard", "clamp", (19, 30, 5), (24, 38, 12)),
    ("CargoSystems", "cargo", (8, 21, 5), (18, 27, 13))],
  "Raider": [
    ("Core", "core", (22, 18, 8), (26, 23, 12)),
    ("Reactor", "power", (22, 10, 8), (26, 15, 12)),
    ("ShieldGenerator", "shield", (22, 24, 8), (26, 27, 12)),
    ("EnginePort", "engine", (4, 0, 4), (15, 16, 14)),
    ("EngineStarboard", "engine", (33, 0, 4), (44, 16, 14)),
    ("Sensors", "sensor", (21, 27, 11), (27, 37, 17)),
    ("WeaponPort", "weapon", (3, 31, 5), (15, 54, 14)),
    ("WeaponStarboard", "weapon", (33, 31, 5), (45, 54, 14))],
}


def Interior(_cells):
  solid = np.pad(_cells != 0, 1)
  inside = solid[1:-1, 1:-1, 1:-1].copy()
  for axis in range(3):
    for side in (-1, 1):
      slices = [slice(1, -1)] * 3
      slices[axis] = slice(2, None) if side == 1 else slice(None, -2)
      inside &= solid[tuple(slices)]
  return inside


def RestoreFrigate(_damaged):
  master = _damaged.copy()
  mirrored = _damaged[::-1].copy()
  # The original authoring built the port half, mirrored it, then cut only starboard.
  missing = (master == 0) & (mirrored != 0)
  missing[:master.shape[0] // 2] = False
  master[missing] = mirrored[missing]
  damage = ((master >= 77) & (master <= 80)) | ((master >= 216) & (master <= 219))
  master[damage] = np.where(mirrored[damage] != 0, mirrored[damage], 61)
  # Three tiny original breach fragments remain outside the old mirrored envelope.
  # Restore their nearest structural ties so the intact reference is one connected object.
  connected = Flood(master != 0, (24, 30, 14))
  loose = np.argwhere((master != 0) & ~connected)
  for point in loose:
    candidates = np.argwhere(connected)
    distances = np.abs(candidates - point).sum(axis=1)
    nearest = candidates[int(np.argmin(distances))]
    if int(distances.min()) > 8:
      raise ValueError("Unexpected distant fragment; inspect the frigate source")
    current = point.copy()
    for axis in (2, 0, 1):
      while current[axis] != nearest[axis]:
        current[axis] += 1 if current[axis] < nearest[axis] else -1
        if master[tuple(current)] == 0:
          master[tuple(current)] = 61
    connected = Flood(master != 0, (24, 30, 14))
  return master


def PaintInternals(_master, _name):
  master = _master.copy()
  inside = Interior(master)
  x, y, z = np.indices(master.shape)
  master[inside] = 4
  master[inside & (z % 5 == 0)] = 68
  master[inside & ((y % 6 == 0) | (x % 8 == 0))] = 60
  master[inside & (np.abs(x - master.shape[0] // 2) <= 1) & (z < 10)] = 63
  colors = {"core": 93, "power": 29, "engine": 59, "sensor": 75, "weapon": 62,
            "shield": 94, "clamp": 35, "cargo": 68, "atmosphere": 19}
  for name, function, low, high in REGIONS[_name]:
    if function == "structure":
      continue
    section = tuple(slice(a, b) for a, b in zip(low, high))
    mask = np.zeros(master.shape, dtype=bool)
    mask[section] = True
    mask &= inside
    master[mask] = colors[function]
    if function == "core":
      master[mask & (y % 3 == 0)] = 220
    elif function == "power":
      master[mask & (y % 4 == 0)] = 61
    elif function == "engine":
      master[mask & (y % 5 == 0)] = 209
    elif function == "clamp":
      master[mask & ((x + y) % 4 < 2)] = 38
  return master


def Runs(_values):
  indices = np.flatnonzero(_values)
  if not len(indices):
    return
  start, previous = int(indices[0]), int(indices[0])
  color = int(_values[start])
  for raw in indices[1:]:
    index = int(raw)
    value = int(_values[index])
    if index != previous + 1 or value != color:
      yield start, previous - start + 1, color
      start, color = index, value
    previous = index
  yield start, previous - start + 1, color


def Derive(_assets):
  catalog = ["# Hull authoring data (ADR-021). Not loaded by the current game.",
             "# VOX coordinates: +Z up, forward +Y. Bounds are min-inclusive, max-exclusive.",
             "catalog name=MvpHulls version=1", ""]
  for name in HULL_NAMES:
    path = _assets / "Models" / (name + "Intact.vox")
    cells, palette = ReadVox(path)
    dimensions = ",".join(map(str, cells.shape))
    catalog.append(f"hull name={name} model={name}Intact legacy={name} size={dimensions} sha256={HashFile(path)}")
    for region, function, low, high in REGIONS[name]:
      catalog.append(f"region hull={name} name={region} function={function} "
                     f"min={','.join(map(str, low))} max={','.join(map(str, high))}")
    catalog.append("")
  folder = _assets / "Hulls"
  folder.mkdir(parents=True, exist_ok=True)
  (folder / "Hulls.txt").write_text("\n".join(catalog), encoding="utf-8")
  masterPath = _assets / "Models" / "FrigateIntact.vox"
  previewPath = _assets / "Models" / "Frigate.vox"
  master, _ = ReadVox(masterPath)
  preview, _ = ReadVox(previewPath)
  if np.any((preview != 0) & (master == 0)):
    raise ValueError("Master cannot reproduce the preview; a legacy solid cell is missing")
  base, target = Flatten(master), Flatten(preview)
  removal = ((base != 0) & (target == 0)).astype(np.uint8)
  fixture = ["# F-317 authoring fixture. Presence and the cosmetic palette reference are separate.",
             "# Index = x + sizeX * (y + sizeY * z). Runs are sorted and never overlap.",
             f"preset name=FrigateF317 version=1 hull=Frigate preview=Frigate "
             f"mastersha256={HashFile(masterPath)} previewsha256={HashFile(previewPath)}", ""]
  fixture.extend(f"remove start={start} count={count}" for start, count, color in Runs(removal))
  fixture.append("")
  fixture.append(f"appearance model=Frigate sha256={HashFile(previewPath)}")
  (folder / "FrigateF317.txt").write_text("\n".join(fixture) + "\n", encoding="utf-8")


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--output", type=Path, default=ROOT / "Assets")
  parser.add_argument("--refresh", action="store_true", help="Keep edited master VOX files; refresh sidecars only")
  options = parser.parse_args()
  (options.output / "Models").mkdir(parents=True, exist_ok=True)
  if not options.refresh:
    for name in HULL_NAMES:
      original = ROOT / "Assets" / "Models" / (name + ".vox")
      cells, palette = ReadVox(original)
      master = RestoreFrigate(cells) if name == "Frigate" else cells.copy()
      master = PaintInternals(master, name)
      WriteVox(options.output / "Models" / (name + "Intact.vox"), master, palette)
      if options.output.resolve() != (ROOT / "Assets").resolve():
        (options.output / "Models" / (name + ".vox")).write_bytes(original.read_bytes())
      print(f"{name}Intact: {np.count_nonzero(master):,} solid cells")
  Derive(options.output)
  print(f"Wrote authoring catalog and initial damage fixture to {options.output / 'Hulls'}")


if __name__ == "__main__":
  Main()
