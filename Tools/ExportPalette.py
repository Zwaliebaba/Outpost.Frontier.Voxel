"""Export the canonical VOX palette as MagicaVoxel's 256x1 RGBA import PNG.

python Tools/ExportPalette.py
python Tools/ExportPalette.py --output /tmp/OutpostFrontier.png
Colors and alpha are exported; PNG cannot carry MagicaVoxel Emit/Glass properties.
Uses only the standard library beyond the existing VOX reader's NumPy dependency.
"""

import argparse
from pathlib import Path
import struct
import zlib

from VoxelAssets import ReadVox

ROOT = Path(__file__).resolve().parent.parent


def Chunk(_name, _content):
  data = _name + _content
  return struct.pack(">I", len(_content)) + data + struct.pack(">I", zlib.crc32(data))


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--output", type=Path, default=ROOT / "Assets/Palettes/OutpostFrontier.png")
  options = parser.parse_args()
  _, palette = ReadVox(ROOT / "Assets/Models/Frigate.vox")
  # RGBA entry c-1 is voxel color c. The unused final entry stays last; never prepend index 0.
  header = struct.pack(">IIBBBBB", 256, 1, 8, 6, 0, 0, 0)
  data = b"\x89PNG\r\n\x1a\n" + Chunk(b"IHDR", header)
  data += Chunk(b"IDAT", zlib.compress(b"\0" + palette, level=9)) + Chunk(b"IEND", b"")
  options.output.parent.mkdir(parents=True, exist_ok=True)
  options.output.write_bytes(data)
  print(f"Wrote {options.output}: 256x1 RGBA, canonical color order and alpha")


if __name__ == "__main__":
  Main()
