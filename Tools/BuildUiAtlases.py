"""Draw the UI sprite atlases and write them as DDS files with their manifest (ADR-023).

python Tools/BuildUiAtlases.py            # writes Assets/Ui/UiMasks.dds, UiFrames.dds and Sprites.txt
python Tools/BuildUiAtlases.py --check    # exit 1 when the committed files differ from what it draws

Standard library only, and deterministic: the same script always writes the same bytes. Every
shape is a signed distance field sampled at texel centres, so edges are antialiased over one texel.

  UiMasks.dds   R8_UNORM coverage: icons, the tintable panel fill and edge, the focus ring
  UiFrames.dds  R8G8B8A8_UNORM_SRGB, straight alpha: button frames per control state, panel frames

Art is authored at twice the 1080-line reference, so the HUD draws it at half its own scale.
Rects sit on 4-texel boundaries with an 8-texel gutter. Transparent colour texels carry the colour
of the nearest drawn texel in their sprite's cell (edge-colour bleed), so linear filtering never
pulls black into an edge. The manifest is written from the same table the atlases are drawn
from, so the two cannot disagree.
"""

import argparse
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "Assets" / "Ui"

GUTTER = 8
DXGI_FORMAT_R8_UNORM = 61
DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29
ALPHA_MODE_UNKNOWN = 0
ALPHA_MODE_STRAIGHT = 1

# ── Signed distances (negative inside), in texels ─────────────────────────────────────────────


def Circle(_x, _y, _cx, _cy, _r):
  return math.hypot(_x - _cx, _y - _cy) - _r


def RoundedBox(_x, _y, _cx, _cy, _hx, _hy, _r):
  qx = abs(_x - _cx) - _hx + _r
  qy = abs(_y - _cy) - _hy + _r
  outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
  return outside + min(max(qx, qy), 0.0) - _r


def Capsule(_x, _y, _ax, _ay, _bx, _by, _r):
  px, py = _x - _ax, _y - _ay
  dx, dy = _bx - _ax, _by - _ay
  t = max(0.0, min(1.0, (px * dx + py * dy) / (dx * dx + dy * dy)))
  return math.hypot(px - dx * t, py - dy * t) - _r


def Polygon(_x, _y, _points):
  """Signed distance to a simple polygon (Inigo Quilez's winding formulation)."""
  d = (_x - _points[0][0]) ** 2 + (_y - _points[0][1]) ** 2
  sign = 1.0
  j = len(_points) - 1
  for i in range(len(_points)):
    ex, ey = _points[j][0] - _points[i][0], _points[j][1] - _points[i][1]
    wx, wy = _x - _points[i][0], _y - _points[i][1]
    t = max(0.0, min(1.0, (wx * ex + wy * ey) / (ex * ex + ey * ey)))
    bx, by = wx - ex * t, wy - ey * t
    d = min(d, bx * bx + by * by)
    c1 = _y >= _points[i][1]
    c2 = _y < _points[j][1]
    c3 = ex * wy > ey * wx
    if (c1 and c2 and c3) or (not c1 and not c2 and not c3):
      sign = -sign
    j = i
  return sign * math.sqrt(d)


def Ring(_distance, _width):
  """A band _width wide just inside the shape whose distance is _distance."""
  return abs(_distance + _width * 0.5) - _width * 0.5


def Coverage(_distance):
  return max(0.0, min(1.0, 0.5 - _distance))


# ── Mask shapes: each takes the texel centre in sprite coordinates and returns a distance ───────


def IconLaunch(_x, _y):
  arrow = Polygon(_x, _y, [(32, 6), (53, 29), (40, 29), (40, 45), (24, 45), (24, 29), (11, 29)])
  pad = RoundedBox(_x, _y, 32, 54, 21, 3.5, 2)
  return min(arrow, pad)


def IconRepair(_x, _y):
  handle = Capsule(_x, _y, 15, 49, 38, 26, 5.5)
  head = Circle(_x, _y, 42, 22, 14)
  jaw = Circle(_x, _y, 51, 13, 8.5)
  hole = Circle(_x, _y, 15, 49, 2.2)
  return max(min(handle, max(head, -jaw)), -hole)


def IconBuild(_x, _y):
  mast = RoundedBox(_x, _y, 18, 34, 3.5, 22, 1)
  boom = Capsule(_x, _y, 18, 12, 55, 12, 3)
  jib = Capsule(_x, _y, 8, 12, 18, 12, 3)
  brace = Capsule(_x, _y, 18, 26, 34, 12, 2)
  cable = Capsule(_x, _y, 46, 12, 46, 30, 1.3)
  block = RoundedBox(_x, _y, 46, 40, 9.5, 8.5, 2)
  base = RoundedBox(_x, _y, 18, 56, 12, 3, 1.5)
  return min(mast, boom, jib, brace, cable, block, base)


def IconYard(_x, _y):
  hull = Polygon(_x, _y, [(15, 24), (41, 24), (55, 32), (41, 40), (15, 40)])
  engine = RoundedBox(_x, _y, 12, 32, 3, 5, 1)
  window = RoundedBox(_x, _y, 34, 32, 3, 2.2, 1)
  top = Capsule(_x, _y, 8, 13, 46, 13, 2.2)
  bottom = Capsule(_x, _y, 8, 51, 46, 51, 2.2)
  back = Capsule(_x, _y, 8, 13, 8, 51, 2.2)
  return min(max(min(hull, engine), -window), top, bottom, back)


def Chevron(_x, _y, _cx, _cy):
  left = Capsule(_x, _y, _cx, _cy - 6, _cx - 10, _cy + 6, 3.2)
  right = Capsule(_x, _y, _cx, _cy - 6, _cx + 10, _cy + 6, 3.2)
  return min(left, right)


def IconFleet(_x, _y):
  return min(Chevron(_x, _y, 32, 17), Chevron(_x, _y, 17, 41), Chevron(_x, _y, 47, 41))


def IconServices(_x, _y):
  distance = 1e9
  for cy in (13, 32, 51):
    for cx in (13, 32, 51):
      distance = min(distance, RoundedBox(_x, _y, cx, cy, 7.5, 7.5, 2.5))
  return distance


def StatusWarning(_x, _y):
  triangle = Polygon(_x, _y, [(32, 10), (55.5, 52), (8.5, 52)]) - 3.5
  stem = Capsule(_x, _y, 32, 24, 32, 37, 3.3)
  dot = Circle(_x, _y, 32, 45.5, 3.5)
  return max(triangle, -min(stem, dot))


def PanelFill(_x, _y, _size=64, _radius=12):
  half = _size * 0.5
  return RoundedBox(_x, _y, half, half, half, half, _radius)


def PanelEdge(_x, _y):
  return Ring(PanelFill(_x, _y), 3)


def FocusRing(_x, _y):
  return Ring(RoundedBox(_x, _y, 24, 24, 24, 24, 10), 3)


# ── Colour frames: fill under an edge ring, both straight alpha, sRGB values ──────────────────

PANEL_FILL = (7, 17, 25, 250)
PANEL_EDGE = (38, 196, 214, 235)


def Frame(_size, _radius, _edgeWidth, _fill, _edge):
  half = _size * 0.5

  def Shade(_x, _y):
    box = RoundedBox(_x, _y, half, half, half, half, _radius)
    fillAlpha = Coverage(box) * _fill[3] / 255.0
    edgeAlpha = Coverage(Ring(box, _edgeWidth)) * _edge[3] / 255.0
    alpha = edgeAlpha + fillAlpha * (1.0 - edgeAlpha)
    if alpha <= 0.0:
      return (0.0, 0.0, 0.0, 0.0)
    color = tuple((_edge[c] * edgeAlpha + _fill[c] * fillAlpha * (1.0 - edgeAlpha)) / alpha for c in range(3))
    return (color[0], color[1], color[2], alpha)

  return Shade


BUTTON_STATES = [
  # name, fill, edge, edge width in texels
  ("Idle", (10, 34, 44, 232), (30, 110, 128, 200), 2),
  ("Hover", (16, 54, 68, 240), (64, 226, 236, 235), 2),
  ("Pressed", (5, 20, 28, 248), (64, 226, 236, 255), 3),
  ("Selected", (18, 78, 94, 245), (64, 226, 236, 255), 3),
  ("Disabled", (18, 24, 28, 200), (72, 86, 94, 160), 2),
  ("Pending", (40, 30, 8, 236), (255, 172, 44, 235), 2),
  ("Confirmed", (10, 44, 22, 236), (76, 232, 112, 235), 2),
  ("Refused", (48, 12, 10, 236), (240, 72, 58, 245), 2),
]

# ── The atlases: (sprite name, x, y, size, slices or None, shape) ─────────────────────────────


def Cells(_size, _count, _y, _x=GUTTER):
  return [(_x + i * (_size + 2 * GUTTER), _y) for i in range(_count)]


def MaskSprites():
  icons = [("Icon.Launch", IconLaunch), ("Icon.Repair", IconRepair), ("Icon.Build", IconBuild), ("Icon.Yard", IconYard),
           ("Icon.Fleet", IconFleet), ("Icon.Services", IconServices)]
  sprites = []
  for (name, shape), (x, y) in zip(icons, Cells(64, len(icons), GUTTER)):
    sprites.append((name, x, y, 64, None, shape))
  row = GUTTER + 64 + 2 * GUTTER
  sprites.append(("Status.Warning", GUTTER, row, 64, None, StatusWarning))
  sprites.append(("Panel.Fill", GUTTER + 80, row, 64, (20, 20, 20, 20), PanelFill))
  sprites.append(("Panel.Edge", GUTTER + 160, row, 64, (20, 20, 20, 20), PanelEdge))
  sprites.append(("Focus.Ring", GUTTER + 240, row, 48, (16, 16, 16, 16), FocusRing))
  return sprites


def FrameSprites():
  sprites = []
  cells = Cells(48, 4, GUTTER) + Cells(48, 4, GUTTER + 64)
  for (name, fill, edge, width), (x, y) in zip(BUTTON_STATES, cells):
    sprites.append(("Button." + name, x, y, 48, (14, 14, 14, 14), Frame(48, 8, width, fill, edge)))
  row = GUTTER + 2 * 64
  sprites.append(("Panel.Frame", GUTTER, row, 64, (20, 20, 20, 20), Frame(64, 12, 3, PANEL_FILL, PANEL_EDGE)))
  sprites.append(("Panel.Inset", GUTTER + 80, row, 48, (16, 16, 16, 16), Frame(48, 10, 2, (12, 30, 40, 238), (30, 110, 128, 190))))
  return sprites


def Quantize(_value):
  return max(0, min(255, int(_value * 255.0 + 0.5)))


def DrawMasks(_width, _height, _sprites):
  texels = bytearray(_width * _height)
  for _, x0, y0, size, _, shape in _sprites:
    for y in range(size):
      for x in range(size):
        texels[(y0 + y) * _width + x0 + x] = Quantize(Coverage(shape(x + 0.5, y + 0.5)))
  return bytes(texels)


def DrawFrames(_width, _height, _sprites):
  texels = bytearray(_width * _height * 4)
  for _, x0, y0, size, _, shade in _sprites:
    for y in range(size):
      for x in range(size):
        r, g, b, a = shade(x + 0.5, y + 0.5)
        at = ((y0 + y) * _width + x0 + x) * 4
        texels[at:at + 4] = bytes((int(r + 0.5), int(g + 0.5), int(b + 0.5), Quantize(a)))
    Bleed(texels, _width, _height, x0 - GUTTER, y0 - GUTTER, size + 2 * GUTTER)
  return bytes(texels)


def Bleed(_texels, _width, _height, _x0, _y0, _cell):
  """Gives every transparent texel of a cell the colour of its nearest drawn texel (breadth first)."""
  x0, y0 = max(0, _x0), max(0, _y0)
  x1, y1 = min(_width, _x0 + _cell), min(_height, _y0 + _cell)
  frontier = [(x, y) for y in range(y0, y1) for x in range(x0, x1) if _texels[(y * _width + x) * 4 + 3] > 0]
  filled = set(frontier)
  while frontier:
    nextFrontier = []
    for x, y in frontier:
      source = (y * _width + x) * 4
      for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
        if x0 <= nx < x1 and y0 <= ny < y1 and (nx, ny) not in filled:
          target = (ny * _width + nx) * 4
          _texels[target:target + 3] = _texels[source:source + 3]
          filled.add((nx, ny))
          nextFrontier.append((nx, ny))
    frontier = nextFrontier


def Dds(_width, _height, _format, _bytesPerTexel, _alphaMode, _payload):
  """The DX10 DDS subset NeuronCore/DdsFile.cpp reads, byte for byte what its WriteDds writes."""
  flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000  # caps, height, width, pitch, pixel format, mip count
  header = struct.pack("<7I", 124, flags, _height, _width, _width * _bytesPerTexel, 0, 1) + bytes(44)
  pixelFormat = struct.pack("<2I4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
  caps = struct.pack("<5I", 0x1000, 0, 0, 0, 0)
  dx10 = struct.pack("<5I", _format, 3, 0, 1, _alphaMode)
  return b"DDS " + header + pixelFormat + caps + dx10 + _payload


def Manifest(_atlases):
  lines = [
    "# The UI sprite manifest (ADR-023). Written by Tools/BuildUiAtlases.py with the atlases it draws;",
    "# edit the script and run it again rather than editing this file.",
    "#",
    "#   atlas name= file= kind=mask|color       a DDS beside this file. mask: R8_UNORM coverage, tinted",
    "#                                           when drawn. color: sRGB RGBA with straight alpha",
    "#   sprite name= atlas= rect=x,y,w,h        texels from the atlas's top-left corner",
    "#          slice=left,top,right,bottom      optional nine-slice margins in texels",
    "#",
    "# Art is authored at twice the 1080-line reference: the HUD draws it at half its own scale.",
    "# Runtime code names sprites, never rects.",
    "",
  ]
  for name, file, kind, sprites in _atlases:
    lines.append(f"atlas name={name} file={file} kind={kind}")
  for name, _, _, sprites in _atlases:
    lines.append("")
    for spriteName, x, y, size, slices, _ in sprites:
      line = f"sprite name={spriteName} atlas={name} rect={x},{y},{size},{size}"
      if slices is not None:
        line += " slice=" + ",".join(str(value) for value in slices)
      lines.append(line)
  return ("\r\n".join(lines) + "\r\n").encode("utf-8")


def Validate(_width, _height, _sprites):
  for name, x, y, size, slices, _ in _sprites:
    assert x % 4 == 0 and y % 4 == 0 and size % 4 == 0, f"{name} is not on 4-texel boundaries"
    assert x >= GUTTER and y >= GUTTER and x + size + GUTTER <= _width and y + size + GUTTER <= _height, f"{name} has no gutter"
    if slices is not None:
      assert slices[0] + slices[2] <= size and slices[1] + slices[3] <= size, f"{name} slices exceed its rect"
  for i, a in enumerate(_sprites):
    for b in _sprites[i + 1:]:
      apart = (a[1] + a[3] + 2 * GUTTER <= b[1] or b[1] + b[3] + 2 * GUTTER <= a[1] or a[2] + a[3] + 2 * GUTTER <= b[2] or
               b[2] + b[3] + 2 * GUTTER <= a[2])
      assert apart, f"{a[0]} and {b[0]} share a gutter"


def Main():
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--output", type=Path, default=OUTPUT)
  parser.add_argument("--check", action="store_true", help="compare with the files on disk instead of writing them")
  options = parser.parse_args()

  masks = MaskSprites()
  frames = FrameSprites()
  Validate(512, 512, masks)
  Validate(256, 256, frames)
  outputs = {
    "UiMasks.dds": Dds(512, 512, DXGI_FORMAT_R8_UNORM, 1, ALPHA_MODE_UNKNOWN, DrawMasks(512, 512, masks)),
    "UiFrames.dds": Dds(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, ALPHA_MODE_STRAIGHT, DrawFrames(256, 256, frames)),
    "Sprites.txt": Manifest([("UiMasks", "UiMasks.dds", "mask", masks), ("UiFrames", "UiFrames.dds", "color", frames)]),
  }
  stale = []
  for name, data in outputs.items():
    path = options.output / name
    if options.check:
      current = path.read_bytes() if path.exists() else b""
      if name.endswith(".txt"):
        current, data = current.replace(b"\r\n", b"\n"), data.replace(b"\r\n", b"\n")
      if current != data:
        stale.append(name)
    else:
      path.parent.mkdir(parents=True, exist_ok=True)
      path.write_bytes(data)
      print(f"Wrote {path} ({len(data)} bytes)")
  if stale:
    print("Out of date: " + ", ".join(stale) + ". Run python Tools/BuildUiAtlases.py", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(Main())
