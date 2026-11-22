"""Check the shared signal/glazing palette, native authoring attributes and model fittings."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from AuthorSignalMaterials import NATURAL, ROOT
from HullAssets import PALETTE_SHA256
from VoxelAssets import AUTHOR_MATERIALS, ReadMaterials, ReadVox, STROBE_YELLOW, WINDOW_BLUE


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--assets", type=Path, default=ROOT / "Assets")
  parser.add_argument("--report", type=Path)
  options = parser.parse_args()
  report = {"paletteSha256": PALETTE_SHA256, "strobeIndex": STROBE_YELLOW, "glassIndex": WINDOW_BLUE,
            "glassRgba": [75, 162, 235, 80], "strobePeriodSeconds": 1.5, "models": {}}
  for path in sorted((options.assets / "Models").glob("*.vox")):
    cells, palette = ReadVox(path)
    if hashlib.sha256(palette).hexdigest() != PALETTE_SHA256:
      raise ValueError(f"Noncanonical palette: {path}")
    colors = np.frombuffer(palette, np.uint8).reshape(256, 4)
    if tuple(colors[STROBE_YELLOW - 1]) != (255, 224, 48, 255):
      raise ValueError("Yellow strobe must carry its exact bright color")
    if tuple(colors[WINDOW_BLUE - 1]) != (75, 162, 235, 80):
      raise ValueError("Blue glazing must be translucent")
    if ReadMaterials(path) != AUTHOR_MATERIALS:
      raise ValueError(f"Missing or stale MagicaVoxel emission/glass attributes: {path}")
    strobes, windows = int(np.count_nonzero(cells == STROBE_YELLOW)), int(np.count_nonzero(cells == WINDOW_BLUE))
    if path.stem in NATURAL and (strobes or windows):
      raise ValueError(f"Unintended fixtures on natural/debris geometry: {path}")
    if path.stem not in NATURAL and strobes < 2:
      raise ValueError(f"Missing distributed manufactured-model strobes: {path}")
    report["models"][path.stem] = {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
      "strobeCells": strobes, "glazingCells": windows}
  if not report["models"]:
    raise ValueError("No editable model assets found")
  if options.report:
    options.report.parent.mkdir(parents=True, exist_ok=True)
    options.report.write_text(json.dumps(report, indent=2) + "\n")
  models = report["models"]
  print(f"PASS: {len(models)} shared palettes and matching MagicaVoxel MATL attributes")
  print(f"PASS: {sum(bool(v['strobeCells']) for v in models.values())} models with yellow strobes; "
        f"{sum(bool(v['glazingCells']) for v in models.values())} with recessed blue glazing")


if __name__ == "__main__":
  Main()
