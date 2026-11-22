"""Validate damage-ready masters, module regions, damage reconstruction and repair."""

import argparse
import json
from pathlib import Path

from HullAssets import ROOT, Validate


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--assets", type=Path, default=ROOT / "Assets")
  parser.add_argument("--report", type=Path)
  options = parser.parse_args()
  report = Validate(options.assets)
  if options.report:
    options.report.parent.mkdir(parents=True, exist_ok=True)
    options.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
  for name, hull in report["hulls"].items():
    print(f"PASS {name}: {hull['solidVoxels']:,} cells; one connected core; "
          f"{len(hull['regions'])} valid regions; clean materials and canonical palette")
  preset = report["preset"]
  print(f"PASS F-317: {preset['removedVoxels']:,} removed cells; {preset['appearanceVoxels']:,} cosmetic overrides")
  print("PASS exact damaged-preview reconstruction and full repair to intact master")
  print(f"PASS midsection severance: {preset['midsectionDetachedVoxels']:,} cells detached from the core")


if __name__ == "__main__":
  Main()
