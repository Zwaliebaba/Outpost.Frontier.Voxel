"""Development-only MagicaVoxel I/O and visible-face meshing (Python 3.10+, NumPy).

Coordinates here are VOX: X right, Y back, Z up. Keep the file palette byte-for-byte;
the game's canonical material table, not a preview renderer, assigns emission.
"""

from __future__ import annotations

import math
import shlex
import struct
from pathlib import Path

import numpy as np

STROBE_YELLOW, WINDOW_BLUE = 224, 225
AUTHOR_MATERIALS = {
  STROBE_YELLOW: {"_type": "_emit", "_weight": "1", "_emit": "1", "_flux": "3", "_rough": "0.2"},
  WINDOW_BLUE: {"_type": "_glass", "_weight": "0.68627451", "_trans": "0.68627451",
                "_rough": "0.05", "_ior": "0.3", "_att": "0"},
}


def ReadMaterials(_path):
  """Read MATL authoring dictionaries without changing the public grid/palette API."""
  data = Path(_path).read_bytes()
  materials = {}
  offset = 20
  while offset < len(data):
    kind, size, children = struct.unpack_from("<4sII", data, offset)
    start = offset + 12
    if kind == b"MATL":
      index, count = struct.unpack_from("<II", data, start)
      at = start + 8
      properties = {}
      for _ in range(count):
        strings = []
        for _ in range(2):
          length = struct.unpack_from("<I", data, at)[0]
          at += 4
          strings.append(data[at:at + length].decode("utf-8"))
          at += length
        if strings[0] in properties or at > start + size:
          raise ValueError("Invalid MATL dictionary")
        properties[strings[0]] = strings[1]
      if index in materials or at != start + size:
        raise ValueError("Duplicate or malformed MATL")
      materials[index] = properties
    offset = start + size + children
  return materials


def ReadVox(_path):
  data = Path(_path).read_bytes()
  if data[:4] != b"VOX " or struct.unpack_from("<I", data, 4)[0] not in (150, 200):
    raise ValueError(f"{_path}: invalid VOX header")
  dimensions, cells, palette = None, None, None

  def Walk(_start, _end):
    nonlocal dimensions, cells, palette
    offset = _start
    while offset < _end:
      if offset + 12 > _end:
        raise ValueError(f"{_path}: truncated header")
      chunkId, contentSize, childrenSize = struct.unpack_from("<4sII", data, offset)
      start = offset + 12
      stop = start + contentSize
      end = stop + childrenSize
      if end > _end:
        raise ValueError(f"{_path}: truncated {chunkId!r}")
      if chunkId == b"SIZE":
        if dimensions is not None or contentSize != 12:
          raise ValueError(f"{_path}: expected one SIZE chunk")
        dimensions = struct.unpack_from("<3I", data, start)
        if not all(1 <= size <= 256 for size in dimensions):
          raise ValueError(f"{_path}: dimensions outside 1..256")
      elif chunkId == b"XYZI":
        if dimensions is None or cells is not None:
          raise ValueError(f"{_path}: expected one XYZI after SIZE")
        count = struct.unpack_from("<I", data, start)[0]
        if contentSize != 4 + 4 * count:
          raise ValueError(f"{_path}: inconsistent voxel count")
        records = np.frombuffer(data, np.uint8, count * 4, start + 4).reshape(-1, 4)
        if not count or np.any(records[:, :3] >= dimensions) or np.any(records[:, 3] == 0):
          raise ValueError(f"{_path}: empty or invalid voxel records")
        cells = np.zeros(dimensions, dtype=np.uint8)
        cells[tuple(records[:, :3].T)] = records[:, 3]
        if np.count_nonzero(cells) != count:
          raise ValueError(f"{_path}: duplicate voxels")
      elif chunkId == b"RGBA":
        if contentSize != 1024 or palette is not None:
          raise ValueError(f"{_path}: expected one 1024-byte RGBA chunk")
        palette = data[start:stop]
      if childrenSize:
        Walk(stop, end)
      offset = end

  if data[8:12] != b"MAIN":
    raise ValueError(f"{_path}: missing MAIN")
  Walk(8, len(data))
  if cells is None or palette is None:
    raise ValueError(f"{_path}: incomplete model")
  return cells, palette


def WriteVox(_path, _cells, _palette, _materials=None):
  if len(_palette) != 1024 or not all(1 <= size <= 256 for size in _cells.shape):
    raise ValueError("Invalid palette or grid")
  # np.nonzero on Z,Y,X gives the engine writer's deterministic Z,Y,X order.
  z, y, x = np.nonzero(_cells.transpose(2, 1, 0))
  records = np.column_stack((x, y, z, _cells[x, y, z])).astype(np.uint8).tobytes()

  def Chunk(_name, _content, _children=b""):
    return struct.pack("<4sII", _name, len(_content), len(_children)) + _content + _children

  children = Chunk(b"SIZE", struct.pack("<3I", *_cells.shape))
  children += Chunk(b"XYZI", struct.pack("<I", len(x)) + records)
  children += Chunk(b"RGBA", _palette)
  if _materials is None:
    # These development tools author this game's assets. Older palettes remain valid inputs.
    first = (STROBE_YELLOW - 1) * 4
    _materials = AUTHOR_MATERIALS if _palette[first:first + 4] == bytes((255, 224, 48, 255)) else {}
  for index, properties in _materials.items():
    content = struct.pack("<II", index, len(properties))
    for key, value in properties.items():
      for text in (key, value):
        encoded = text.encode("utf-8")
        content += struct.pack("<I", len(encoded)) + encoded
    children += Chunk(b"MATL", content)
  Path(_path).write_bytes(b"VOX " + struct.pack("<I", 150) + Chunk(b"MAIN", b"", children))


def SurfaceCount(_cells):
  padded = np.pad(_cells != 0, 1)
  inside = padded[1:-1, 1:-1, 1:-1].copy()
  for axis in range(3):
    for direction in (-1, 1):
      slices = [slice(1, -1)] * 3
      slices[axis] = slice(2, None) if direction == 1 else slice(None, -2)
      inside &= padded[tuple(slices)]
  return int(np.count_nonzero((_cells != 0) & ~inside))


def MeshFaces(_cells):
  """Greedily merge coplanar exposed faces of the same palette entry.

  This changes only preview topology: never the source voxels or silhouettes.
  Returns unwelded quad vertices, faces, palette indices with outward winding.
  """
  vertices, faces, colors = [], [], []
  occupied = _cells != 0
  for axis in range(3):
    u, v = (axis + 1) % 3, (axis + 2) % 3
    for direction in (-1, 1):
      neighbor = np.roll(occupied, -direction, axis=axis)
      edge = [slice(None)] * 3
      edge[axis] = -1 if direction == 1 else 0
      neighbor[tuple(edge)] = False
      neighborColors = np.roll(_cells, -direction, axis=axis)
      neighborColors[tuple(edge)] = 0
      throughGlass = (neighborColors == WINDOW_BLUE) & (_cells != WINDOW_BLUE)
      visible = np.where(occupied & (~neighbor | throughGlass), _cells, 0)
      for position in range(_cells.shape[axis]):
        mask = np.take(visible, position, axis=axis)
        remainingAxes = [i for i in range(3) if i != axis]
        if remainingAxes != [u, v]:
          mask = mask.T
        mask = mask.copy()
        for row in range(mask.shape[0]):
          for column in np.flatnonzero(mask[row]):
            color = int(mask[row, column])
            if not color:
              continue
            width = 1
            while column + width < mask.shape[1] and mask[row, column + width] == color:
              width += 1
            height = 1
            while row + height < mask.shape[0] and np.all(mask[row + height, column:column + width] == color):
              height += 1
            mask[row:row + height, column:column + width] = 0
            points = []
            for a, b in ((row, column), (row + height, column),
                         (row + height, column + width), (row, column + width)):
              point = [0, 0, 0]
              point[axis] = position + (1 if direction == 1 else 0)
              point[u], point[v] = a, b
              points.append(tuple(point))
            start = len(vertices)
            vertices.extend(points)
            face = (start, start + 1, start + 2, start + 3)
            faces.append(face if direction == 1 else tuple(reversed(face)))
            colors.append(color)
  return vertices, faces, colors


def ParseScene(_path):
  records = {}
  for line in Path(_path).read_text(encoding="utf-8").splitlines():
    tokens = shlex.split(line, comments=True)
    if tokens:
      values = dict(token.split("=", 1) for token in tokens[1:])
      records.setdefault(tokens[0], []).append(values)
  return records


def Triple(_value):
  return np.array([float(n) for n in _value.split(",")])


def Rotation(_angles):
  yaw, pitch, roll = np.radians(_angles)
  cy, sy, cp, sp, cr, sr = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll)
  yawMatrix = np.array(((cy, 0, sy), (0, 1, 0), (-sy, 0, cy)))
  pitchMatrix = np.array(((1, 0, 0), (0, cp, -sp), (0, sp, cp)))
  rollMatrix = np.array(((cr, -sr, 0), (sr, cr, 0), (0, 0, 1)))
  return yawMatrix @ pitchMatrix @ rollMatrix


SWIZZLE = np.eye(4)[[0, 2, 1, 3]]


def SocketMatrix(_socket, _model, _dimensions):
  pivot = Triple(_model["pivot"]) if "pivot" in _model else np.array(_dimensions) / 2
  position = (Triple(_socket["position"]) - pivot)[[0, 2, 1]] * float(_model.get("voxelsize", 1))
  normal, up = Triple(_socket["normal"])[[0, 2, 1]], Triple(_socket["up"])[[0, 2, 1]]
  normal /= np.linalg.norm(normal)
  up -= np.dot(up, normal) * normal
  up /= np.linalg.norm(up)
  matrix = np.eye(4)
  matrix[:3, :3] = np.column_stack((np.cross(up, normal), up, normal))
  matrix[:3, 3] = position
  return matrix


def InstanceMatrices(_records, _grids):
  models = {record["name"]: record for record in _records["model"]}
  instances = {record["name"]: record for record in _records["instance"]}
  sockets = {(record["model"], record["name"]): record for record in _records.get("socket", [])}
  matrices = {}

  def Resolve(_name, _ancestors=()):
    if _name in matrices:
      return matrices[_name]
    if _name in _ancestors or len(_ancestors) > 15:
      raise ValueError(f"Cyclic/deep attachment: {_name}")
    instance = instances[_name]
    matrix = np.eye(4)
    if "parent" in instance:
      parent = instances[instance["parent"]]
      parentSocket = sockets[parent["model"], instance["parentsocket"]]
      childSocket = sockets[instance["model"], instance["socket"]]
      parentFrame = SocketMatrix(parentSocket, models[parent["model"]], _grids[parent["model"]].shape)
      childFrame = SocketMatrix(childSocket, models[instance["model"]], _grids[instance["model"]].shape)
      halfTurn = np.diag((-1, 1, -1, 1))
      matrix = Resolve(parent["name"], (*_ancestors, _name)) @ parentFrame @ halfTurn @ np.linalg.inv(childFrame)
    else:
      matrix[:3, :3] = Rotation(Triple(instance.get("rotation", "0,0,0"))) * float(instance.get("scale", 1))
      matrix[:3, 3] = Triple(instance.get("position", "0,0,0"))
    matrices[_name] = matrix
    return matrix

  for name in instances:
    Resolve(name)
  return matrices
