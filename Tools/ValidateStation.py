"""Validate authored VOX files, scene references, socket mating and physical station contacts.

python Tools/ValidateStation.py [--assets Assets] [--baseline /path/to/original/Assets]
Requires NumPy. This complements, and does not replace, the Windows scene tests and GPU smoke test.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

from VoxelAssets import InstanceMatrices, ParseScene, ReadVox, SocketMatrix, SurfaceCount, SWIZZLE, Triple, WriteVox

ROOT = Path(__file__).resolve().parent.parent
PALETTE_SHA256 = "afec7a9fbc00aabad89d1c8e20987cc11216208262c3797e2ad3517a916aee86"
STATION_MODELS = {"HangarBay", "HangarSection", "CommandBlock", "TankCluster", "DockingCollar", "TrussBeam",
                  "TrussTower", "Habitat", "Reactor", "Refinery", "TurretPlatform", "ShieldGenerator", "Yard",
                  "RadiatorArray"}


def Unique(_records, _keys):
  result = {}
  for record in _records:
    key = tuple(record[field] for field in _keys)
    if key in result:
      raise ValueError(f"Duplicate identifier: {key}")
    result[key] = record
  return result


def VoxMatrix(_model, _cells, _instanceMatrix):
  pivot = Triple(_model["pivot"]) if "pivot" in _model else np.array(_cells.shape) / 2
  voxelSize = float(_model.get("voxelsize", 1))
  local = np.eye(4)
  local[:3, :3] *= voxelSize
  local[:3, 3] = -pivot * voxelSize
  return _instanceMatrix @ SWIZZLE @ local


def Transform(_points, _matrix):
  return _points @ _matrix[:3, :3].T + _matrix[:3, 3]


def StationContacts(_instances, _models, _grids, _matrices):
  """Check real occupied geometry rather than just overlapping grid bounding boxes.

  One architectural voxel (0.75 world units) is the contact tolerance. Only station
  instances participate; the blueprint is intentionally not a physical structure.
  """
  structures = {}
  for name, instance in _instances.items():
    if instance["model"] not in STATION_MODELS or instance.get("style") == "blueprint":
      continue
    cells = _grids[instance["model"]]
    matrix = VoxMatrix(_models[instance["model"]], cells, _matrices[name])
    occupied = np.column_stack(np.nonzero(cells))
    low, high = occupied.min(axis=0), occupied.max(axis=0) + 1
    corners = np.array(list(itertools.product(*zip(low, high))))
    corners = Transform(corners, matrix)
    # Every solid center participates, including an anchor enclosed by another module.
    points = Transform(occupied + 0.5, matrix)
    structures[name] = (cells, matrix, points, corners.min(axis=0), corners.max(axis=0))
  graph = {name: set() for name in structures}
  offsets = np.vstack((np.zeros((1, 3)), np.eye(3) * .75, -np.eye(3) * .75))
  for a, b in itertools.combinations(structures, 2):
    cellsA, matrixA, pointsA, lowA, highA = structures[a]
    cellsB, matrixB, pointsB, lowB, highB = structures[b]
    if np.any(np.maximum(lowA - highB, lowB - highA) > .751):
      continue
    candidates = pointsA[np.all((pointsA >= lowB - .751) & (pointsA <= highB + .751), axis=1)]
    inverse = np.linalg.inv(matrixB)
    contact = False
    for offset in offsets:
      indices = np.floor(Transform(candidates + offset, inverse) + 1e-7).astype(int)
      valid = np.all((indices >= 0) & (indices < cellsB.shape), axis=1)
      indices = indices[valid]
      if len(indices) and np.any(cellsB[tuple(indices.T)]):
        contact = True
        break
    if contact:
      graph[a].add(b)
      graph[b].add(a)
  visited = set()
  pending = ["Hangar"]
  while pending:
    name = pending.pop()
    if name not in visited:
      visited.add(name)
      pending.extend(graph[name] - visited)
  disconnected = sorted(set(graph) - visited)
  if disconnected:
    raise ValueError(f"Station components disconnected from Hangar: {', '.join(disconnected)}")
  low = np.array([structure[3] for structure in structures.values()]).min(axis=0)
  high = np.array([structure[4] for structure in structures.values()]).max(axis=0)
  return {"solidInstances": len(structures), "contactEdges": sum(map(len, graph.values())) // 2,
          "boundsMinWorld": low.tolist(), "boundsMaxWorld": high.tolist(), "sizeWorld": (high - low).tolist(),
          "contacts": {name: sorted(neighbors) for name, neighbors in graph.items()}}


def Validate(_assets, _baseline=None):
  report = {"models": {}}
  grids = {}
  with tempfile.TemporaryDirectory() as temporary:
    for path in sorted((_assets / "Models").glob("*.vox")):
      cells, palette = ReadVox(path)
      if hashlib.sha256(palette).hexdigest() != PALETTE_SHA256:
        raise ValueError(f"Noncanonical palette: {path}")
      output = Path(temporary) / path.name
      WriteVox(output, cells, palette)
      roundTrip, roundPalette = ReadVox(output)
      if not np.array_equal(roundTrip, cells) or roundPalette != palette:
        raise ValueError(f"Round-trip mismatch: {path}")
      grids[path.stem] = cells
      statistics = {"dimensions": list(cells.shape), "solidVoxels": int(np.count_nonzero(cells)),
                    "surfaceVoxels": SurfaceCount(cells), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
      if _baseline and (_baseline / "Models" / path.name).exists():
        original, originalPalette = ReadVox(_baseline / "Models" / path.name)
        statistics["beforeSurfaceVoxels"] = SurfaceCount(original)
        statistics["changed"] = not np.array_equal(original, cells)
      report["models"][path.stem] = statistics
  records = ParseScene(_assets / "Scene.txt")
  allowed = {"camera", "keylight", "stars", "model", "socket", "instance", "ship", "engine", "light",
             "objective", "resource", "build", "select", "player"}
  if set(records) - allowed:
    raise ValueError(f"Unknown record types: {set(records) - allowed}")
  models = {key[0]: value for key, value in Unique(records["model"], ("name",)).items()}
  instances = {key[0]: value for key, value in Unique(records["instance"], ("name",)).items()}
  for player in records.get("player", []):
    if player.get("instance") not in instances:
      raise ValueError(f"Player references a missing instance: {player}")
  sockets = Unique(records.get("socket", []), ("model", "name"))
  for model in models.values():
    if not (_assets / model["file"]).is_file() or Path(model["file"]).stem not in grids:
      raise ValueError(f"Missing model: {model}")
    if float(model.get("voxelsize", 1)) <= 0:
      raise ValueError(f"Invalid voxel size: {model}")
  for socket in sockets.values():
    cells = grids[socket["model"]]
    position = Triple(socket["position"])
    if np.any(position < 0) or np.any(position > cells.shape):
      raise ValueError(f"Socket out of bounds: {socket}")
  for instance in instances.values():
    if instance["model"] not in models or float(instance.get("scale", 1)) <= 0:
      raise ValueError(f"Invalid instance: {instance}")
  matrices = InstanceMatrices(records, grids)
  for instance in instances.values():
    if "parent" not in instance:
      continue
    parent = instances[instance["parent"]]
    parentSocket = sockets[parent["model"], instance["parentsocket"]]
    childSocket = sockets[instance["model"], instance["socket"]]
    parentFrame = matrices[parent["name"]] @ SocketMatrix(parentSocket, models[parent["model"]], grids[parent["model"]].shape)
    childFrame = matrices[instance["name"]] @ SocketMatrix(childSocket, models[instance["model"]], grids[instance["model"]].shape)
    if not (np.allclose(parentFrame[:3, 3], childFrame[:3, 3], atol=1e-5)
            and np.allclose(parentFrame[:3, 2], -childFrame[:3, 2], atol=1e-5)
            and np.allclose(parentFrame[:3, 1], childFrame[:3, 1], atol=1e-5)):
      raise ValueError(f"Mismatched socket attachment: {instance['name']}")
  for kind in ("ship", "engine", "light", "select"):
    for record in records.get(kind, []):
      if record.get("instance") and record["instance"] not in instances:
        raise ValueError(f"Dangling {kind} reference: {record}")
      if kind in ("engine", "light") and record.get("instance"):
        modelName = instances[record["instance"]]["model"]
        position = Triple(record["position"])
        if np.any(position < 0) or np.any(position > grids[modelName].shape):
          raise ValueError(f"Out-of-bounds {kind}: {record}")
  for record in records.get("build", []):
    if record["model"] not in models:
      raise ValueError(f"Missing build model: {record}")
  # Every shipped VOX reaches the executable through the post-build copy of Assets (ADR-024), which
  # both configurations must run.
  namespace = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
  tree = ET.parse(ROOT / "Outpost/Outpost.vcxproj")
  commands = [node.text or "" for node in tree.findall(".//m:ItemDefinitionGroup/m:PostBuildEvent/m:Command", namespace)]
  if len(commands) != 2 or not all('"$(SolutionDir)Assets" "$(OutDir)Assets"' in command for command in commands):
    raise ValueError("Outpost/Outpost.vcxproj must copy Assets to the output directory in Debug and Release")
  report["station"] = StationContacts(instances, models, grids, matrices)
  report["sceneInstances"] = len(instances)
  sceneText = (_assets / "Scene.txt").read_text(encoding="utf-8")
  report["sceneTextSha256"] = hashlib.sha256(sceneText.encode("utf-8")).hexdigest()
  report["paletteSha256"] = PALETTE_SHA256
  return report


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--assets", type=Path, default=ROOT / "Assets")
  parser.add_argument("--baseline", type=Path)
  parser.add_argument("--report", type=Path)
  options = parser.parse_args()
  report = Validate(options.assets, options.baseline)
  if options.report:
    options.report.parent.mkdir(parents=True, exist_ok=True)
    options.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
  print(f"PASS: {len(report['models'])} VOX files; canonical palettes; valid dimensions; lossless round trips")
  print(f"PASS: {report['sceneInstances']} scene instances; references; socket origins/normals/up; light/engine bounds")
  print("PASS: Debug/Release asset-copy registration and Visual Studio filters")
  station = report["station"]
  print(f"PASS: {station['solidInstances']} connected station modules; {station['contactEdges']} physical contact edges")
  print(f"Station occupied bounds (world X,Y,Z): {station['sizeWorld']}")


if __name__ == "__main__":
  Main()
