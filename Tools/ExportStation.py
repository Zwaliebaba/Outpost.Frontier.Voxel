"""Export a standalone, assembled MagicaVoxel review scene, outside the game's Assets/Models.

python Tools/ExportStation.py --output /tmp/FrontierStation.vox
The game uses the precise component files plus Scene.txt, not this multi-model review export.
This export samples solid station geometry onto a shared 0.75-unit lattice; the ghost plan,
rock, ships and effects are excluded. Each 256-cube tile has a standard VOX scene-graph node.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import struct
from pathlib import Path

import numpy as np

from ValidateStation import ROOT, STATION_MODELS, Transform, VoxMatrix
from VoxelAssets import InstanceMatrices, ParseScene, ReadVox

VOXEL_SIZE = .75
TILE_SIZE = 256


def Chunk(_name, _content, _children=b""):
  return struct.pack("<4sII", _name.encode("ascii"), len(_content), len(_children)) + _content + _children


def Dictionary(_items):
  result = struct.pack("<i", len(_items))
  for key, value in _items.items():
    for text in (key, value):
      data = str(text).encode("utf-8")
      result += struct.pack("<i", len(data)) + data
  return result


def TransformNode(_node, _child, _name, _translation):
  content = struct.pack("<i", _node) + Dictionary({"_name": _name})
  content += struct.pack("<4i", _child, -1, 0, 1)
  content += Dictionary({"_r": "4", "_t": " ".join(map(str, _translation))})
  return Chunk("nTRN", content)


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--assets", type=Path, default=ROOT / "Assets")
  parser.add_argument("--output", type=Path, required=True)
  options = parser.parse_args()
  records = ParseScene(options.assets / "Scene.txt")
  models = {model["name"]: model for model in records["model"]}
  grids = {}
  for name, model in models.items():
    cells, palette = ReadVox(options.assets / model["file"])
    grids[name] = cells
  matrices = InstanceMatrices(records, grids)
  allPoints, allColors = [], []
  for instance in records["instance"]:
    name, modelName = instance["name"], instance["model"]
    if modelName not in STATION_MODELS or instance.get("style") == "blueprint":
      continue
    cells = grids[modelName]
    indices = np.column_stack(np.nonzero(cells))
    matrix = VoxMatrix(models[modelName], cells, matrices[name])
    points = Transform(indices + .5, matrix)[:, [0, 2, 1]] / VOXEL_SIZE
    allPoints.append(np.floor(points + 1e-7).astype(np.int32))
    allColors.append(cells[tuple(indices.T)])
  points, colors = np.concatenate(allPoints), np.concatenate(allColors)
  low = points.min(axis=0)
  size = points.max(axis=0) - low + 1
  assembly = np.zeros(tuple(size), dtype=np.uint8)
  assembly[tuple((points - low).T)] = colors
  tileLow = np.floor_divide(low, TILE_SIZE)
  tileHigh = np.floor_divide(low + size - 1, TILE_SIZE)
  tiles = []
  for tile in itertools.product(*(range(a, b + 1) for a, b in zip(tileLow, tileHigh))):
    origin = np.array(tile) * TILE_SIZE
    start, end = np.maximum(origin, low), np.minimum(origin + TILE_SIZE, low + size)
    subgrid = assembly[tuple(slice(a, b) for a, b in zip(start - low, end - low))]
    z, y, x = np.nonzero(subgrid.transpose(2, 1, 0))
    if not len(x):
      continue
    local = np.column_stack((x, y, z)) + start - origin
    records = np.column_stack((local, subgrid[x, y, z])).astype(np.uint8)
    tiles.append((origin, records))
  children = Chunk("PACK", struct.pack("<i", len(tiles)))
  for origin, voxelRecords in tiles:
    children += Chunk("SIZE", struct.pack("<3i", TILE_SIZE, TILE_SIZE, TILE_SIZE))
    children += Chunk("XYZI", struct.pack("<i", len(voxelRecords)) + voxelRecords.tobytes())
  children += Chunk("RGBA", palette)
  for colorIndex in range(200, 224):
    children += Chunk("MATL", struct.pack("<i", colorIndex)
                      + Dictionary({"_type": "_emit", "_weight": "0.65", "_flux": "1.0", "_rough": "0.5"}))
  children += TransformNode(0, 1, "Frontier industrial station - review assembly", (0, 0, 0))
  nodeIds = [2 + index * 2 for index in range(len(tiles))]
  children += Chunk("nGRP", struct.pack("<i", 1) + Dictionary({}) + struct.pack("<i", len(tiles))
                    + b"".join(struct.pack("<i", node) for node in nodeIds))
  # Independent reconstruction pins tile placement and catches seams or lost voxels.
  reconstructed = np.zeros_like(assembly)
  for index, (origin, voxelRecords) in enumerate(tiles):
    node = nodeIds[index]
    translation = origin + TILE_SIZE // 2
    children += TransformNode(node, node + 1, f"Station tile {index + 1:02}", translation)
    children += Chunk("nSHP", struct.pack("<i", node + 1) + Dictionary({})
                      + struct.pack("<2i", 1, index) + Dictionary({}))
    restoredPoints = voxelRecords[:, :3].astype(np.int32) - TILE_SIZE // 2 + translation - low
    reconstructed[tuple(restoredPoints.T)] = voxelRecords[:, 3]
  children += Chunk("LAYR", struct.pack("<i", 0) + Dictionary({"_name": "Solid station"}) + struct.pack("<i", -1))
  if not np.array_equal(reconstructed, assembly):
    raise ValueError("Review assembly reconstruction failed")
  data = b"VOX " + struct.pack("<i", 150) + Chunk("MAIN", b"", children)
  options.output.parent.mkdir(parents=True, exist_ok=True)
  options.output.write_bytes(data)
  print(f"{options.output}: {len(tiles)} scene-graph tiles, {np.count_nonzero(assembly):,} voxels")
  print(f"PASS: exact tiled reconstruction, canonical palette, 256-voxel maximum dimension; {len(data):,} bytes")
  print(f"SHA256: {hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
  Main()
