"""Asset-authoring hull regions and initial damage fixtures; not a runtime mask protocol.

Run-length indices use x + sizeX * (y + sizeY * z). Regions use half-open VOX bounds.
Presence and cosmetic palette overrides stay separate: simulation materials come from the master.
Python 3.10+ and NumPy; no game dependency is introduced.
"""

from __future__ import annotations

from collections import deque
import hashlib
from pathlib import Path
import shlex

import numpy as np

from VoxelAssets import ReadVox, SurfaceCount

ROOT = Path(__file__).resolve().parent.parent
HULL_NAMES = ("Frigate", "EscortLancer", "EscortTalon", "Tug", "Raider")
FUNCTIONS = {"core", "power", "engine", "sensor", "weapon", "shield", "clamp", "cargo", "structure", "atmosphere"}
PALETTE_SHA256 = "afec7a9fbc00aabad89d1c8e20987cc11216208262c3797e2ad3517a916aee86"


def HashFile(_path):
  return hashlib.sha256(Path(_path).read_bytes()).hexdigest()


def Flatten(_cells):
  return _cells.transpose(2, 1, 0).reshape(-1).copy()


def Expand(_cells, _dimensions):
  x, y, z = _dimensions
  return _cells.reshape(z, y, x).transpose(2, 1, 0).copy()


def ReadRecords(_path, _schema):
  result = []
  for lineNumber, line in enumerate(Path(_path).read_text(encoding="utf-8").splitlines(), 1):
    tokens = shlex.split(line, comments=True)
    if not tokens:
      continue
    kind = tokens[0]
    if kind not in _schema:
      raise ValueError(f"{_path}:{lineNumber}: unknown keyword {kind}")
    values = {}
    for token in tokens[1:]:
      if "=" not in token:
        raise ValueError(f"{_path}:{lineNumber}: expected key=value")
      key, value = token.split("=", 1)
      if key in values:
        raise ValueError(f"{_path}:{lineNumber}: duplicate key {key}")
      values[key] = value
    if set(values) != set(_schema[kind]):
      raise ValueError(f"{_path}:{lineNumber}: wrong keys for {kind}: {sorted(values)}")
    result.append((kind, values))
  return result


def Triple(_value):
  values = tuple(int(value) for value in _value.split(","))
  if len(values) != 3:
    raise ValueError("Expected three integers")
  return values


def Identifier(_name):
  if not _name.isascii() or not _name[0:1].isupper() or not _name.isalnum():
    raise ValueError(f"Invalid asset identifier: {_name}")
  return _name


def RegionSlice(_region):
  return tuple(slice(a, b) for a, b in zip(Triple(_region["min"]), Triple(_region["max"])))


def Flood(_occupied, _seed):
  """Six-neighbor flood, with the same deterministic grid order as the fixture runs."""
  sizeX, sizeY, sizeZ = _occupied.shape
  plane = sizeX * sizeY
  solid = Flatten(_occupied).astype(bool)
  visited = np.zeros(len(solid), dtype=bool)
  start = int(_seed[0] + sizeX * (_seed[1] + sizeY * _seed[2]))
  if not solid[start]:
    return Expand(visited, _occupied.shape)
  pending = deque([start])
  visited[start] = True
  while pending:
    index = pending.popleft()
    x, y, z = index % sizeX, index // sizeX % sizeY, index // plane
    neighbors = []
    if x > 0:
      neighbors.append(index - 1)
    if x + 1 < sizeX:
      neighbors.append(index + 1)
    if y > 0:
      neighbors.append(index - sizeX)
    if y + 1 < sizeY:
      neighbors.append(index + sizeX)
    if z > 0:
      neighbors.append(index - plane)
    if z + 1 < sizeZ:
      neighbors.append(index + plane)
    for neighbor in neighbors:
      if solid[neighbor] and not visited[neighbor]:
        visited[neighbor] = True
        pending.append(neighbor)
  return Expand(visited, _occupied.shape)


def CoreSeed(_master, _regions):
  core = next(region for region in _regions if region["function"] == "core")
  minimum = np.array(Triple(core["min"]))
  points = np.argwhere(_master[RegionSlice(core)] != 0)
  if not len(points):
    raise ValueError("Core region contains no solid cells")
  return tuple((points[0] + minimum).tolist())


def ReadCatalog(_assets):
  schema = {"catalog": ("name", "version"), "hull": ("name", "model", "legacy", "size", "sha256"),
            "region": ("hull", "name", "function", "min", "max")}
  hulls, regions = {}, {}
  records = ReadRecords(Path(_assets) / "Hulls" / "Hulls.txt", schema)
  if not records or records[0] != ("catalog", {"name": "MvpHulls", "version": "1"}):
    raise ValueError("Expected version 1 MvpHulls catalog")
  if sum(kind == "catalog" for kind, values in records) != 1:
    raise ValueError("Duplicate catalog header")
  for kind, values in records[1:]:
    if kind == "hull":
      name = Identifier(values["name"])
      Identifier(values["model"])
      Identifier(values["legacy"])
      if name in hulls:
        raise ValueError(f"Duplicate hull: {name}")
      hulls[name] = values
    else:
      Identifier(values["name"])
      Identifier(values["hull"])
      regions.setdefault(values["hull"], []).append(values)
  if set(hulls) != set(HULL_NAMES) or set(regions) != set(hulls):
    raise ValueError("Expected exactly the five MVP vessel hulls and matching regions")
  grids, palette = {}, None
  for name, hull in hulls.items():
    path = Path(_assets) / "Models" / (hull["model"] + ".vox")
    master, currentPalette = ReadVox(path)
    if HashFile(path) != hull["sha256"] or master.shape != Triple(hull["size"]):
      raise ValueError(f"Stale master hash or dimensions: {name}")
    if hashlib.sha256(currentPalette).hexdigest() != PALETTE_SHA256:
      raise ValueError(f"Noncanonical palette: {name}")
    if np.any(((master >= 77) & (master <= 80)) | ((master >= 216) & (master <= 219))):
      raise ValueError(f"Baked damage material in intact hull: {name}")
    legacy, legacyPalette = ReadVox(Path(_assets) / "Models" / (hull["legacy"] + ".vox"))
    if legacy.shape != master.shape or legacyPalette != currentPalette:
      raise ValueError(f"Legacy/master grids or palettes differ: {name}")
    ids = set()
    for region in regions[name]:
      if region["name"] in ids or region["function"] not in FUNCTIONS:
        raise ValueError(f"Duplicate region or unknown function: {name}")
      ids.add(region["name"])
      low, high = np.array(Triple(region["min"])), np.array(Triple(region["max"]))
      if np.any(low < 0) or np.any(high > master.shape) or np.any(low >= high):
        raise ValueError(f"Out-of-bounds region: {name}/{region['name']}")
      if not np.count_nonzero(master[RegionSlice(region)]):
        raise ValueError(f"Region contains no master voxels: {name}/{region['name']}")
    if sum(region["function"] == "core" for region in regions[name]) != 1:
      raise ValueError(f"Expected one core: {name}")
    core = next(region for region in regions[name] if region["function"] == "core")
    if not np.all(master[RegionSlice(core)]):
      raise ValueError(f"Core region must be fully occupied: {name}")
    if np.count_nonzero(Flood(master != 0, CoreSeed(master, regions[name]))) != np.count_nonzero(master):
      raise ValueError(f"Master has cells disconnected from its core: {name}")
    grids[name] = master
    palette = currentPalette
  return hulls, regions, grids, palette


def ReadPreset(_path, _master, _expectedHull, _expectedHash):
  schema = {"preset": ("name", "version", "hull", "preview", "mastersha256", "previewsha256"),
            "remove": ("start", "count"), "appearance": ("model", "sha256")}
  records = ReadRecords(_path, schema)
  if not records or records[0][0] != "preset" or sum(kind == "preset" for kind, _ in records) != 1:
    raise ValueError("Expected one preset header before spans")
  header = records[0][1]
  Identifier(header["name"])
  Identifier(header["preview"])
  if header["version"] != "1" or header["hull"] != _expectedHull or header["mastersha256"] != _expectedHash:
    raise ValueError("Preset does not match its master")
  base = Flatten(_master)
  present = base != 0
  appearance = np.zeros(len(base), dtype=np.uint8)
  removed = np.zeros(len(base), dtype=bool)
  previous = -1
  appearanceStarted = False
  for kind, values in records[1:]:
    if kind == "appearance":
      if appearanceStarted:
        raise ValueError("Duplicate appearance reference")
      appearanceStarted = True
      name = Identifier(values["model"])
      if name != header["preview"] or values["sha256"] != header["previewsha256"]:
        raise ValueError("Appearance reference must match the declared preview")
      path = Path(_path).parent.parent / "Models" / (name + ".vox")
      reference, palette = ReadVox(path)
      if HashFile(path) != values["sha256"] or reference.shape != _master.shape:
        raise ValueError("Stale appearance hash or dimensions")
      if hashlib.sha256(palette).hexdigest() != PALETTE_SHA256:
        raise ValueError("Appearance reference has a noncanonical palette")
      colors = Flatten(reference)
      if not np.array_equal(colors != 0, present):
        raise ValueError("Appearance occupancy differs from the structural mask")
      appearance = np.where(present & (colors != base), colors, 0).astype(np.uint8)
      continue
    elif appearanceStarted:
      raise ValueError("All remove spans must precede appearance spans")
    start, count = int(values["start"]), int(values["count"])
    stop = start + count
    if count <= 0 or start < 0 or stop > len(base) or start <= previous:
      raise ValueError(f"Out-of-bounds, unordered or overlapping {kind} span")
    previous = stop - 1
    if np.any(base[start:stop] == 0):
      raise ValueError(f"{kind} span addresses empty archetype cells")
    present[start:stop] = False
    removed[start:stop] = True
  return header, Expand(present, _master.shape), Expand(appearance, _master.shape), int(np.count_nonzero(removed))


def BakePreview(_master, _present, _appearance):
  output = np.where(_present, _master, 0).astype(np.uint8)
  overrides = (_appearance != 0) & _present
  output[overrides] = _appearance[overrides]
  return output


def Validate(_assets):
  hulls, regions, grids, palette = ReadCatalog(_assets)
  report = {"hulls": {}, "paletteSha256": PALETTE_SHA256}
  for name, master in grids.items():
    report["hulls"][name] = {"model": hulls[name]["model"], "dimensions": list(master.shape),
      "solidVoxels": int(np.count_nonzero(master)), "surfaceVoxels": SurfaceCount(master),
      "sha256": hulls[name]["sha256"], "coreConnected": True,
      "regions": [{"name": region["name"], "function": region["function"],
                   "solidVoxels": int(np.count_nonzero(master[RegionSlice(region)]))} for region in regions[name]]}
  header, present, appearance, removed = ReadPreset(Path(_assets) / "Hulls" / "FrigateF317.txt", grids["Frigate"],
                                                  "Frigate", hulls["Frigate"]["sha256"])
  previewPath = Path(_assets) / "Models" / (header["preview"] + ".vox")
  expected, _ = ReadVox(previewPath)
  if HashFile(previewPath) != header["previewsha256"] or not np.array_equal(
      BakePreview(grids["Frigate"], present, appearance), expected):
    raise ValueError("Frigate preset does not reproduce the exact shipped preview")
  if not np.array_equal(present, expected != 0):
    raise ValueError("Presence alone must reproduce the preview occupancy")
  repaired = BakePreview(grids["Frigate"], grids["Frigate"] != 0, np.zeros_like(appearance))
  if not np.array_equal(repaired, grids["Frigate"]):
    raise ValueError("Repair does not restore the clean master")
  cut = grids["Frigate"] != 0
  cut[:, 70:73, :] = False
  connected = Flood(cut, CoreSeed(grids["Frigate"], regions["Frigate"]))
  detached = cut & ~connected
  if not np.count_nonzero(detached[:, 104:, :]):
    raise ValueError("Midsection severance failed to detach the bow")
  report["preset"] = {"name": header["name"], "removedVoxels": removed,
                      "appearanceVoxels": int(np.count_nonzero(appearance)), "previewExact": True,
                      "repairExact": True, "midsectionDetachedVoxels": int(np.count_nonzero(detached))}
  return report
