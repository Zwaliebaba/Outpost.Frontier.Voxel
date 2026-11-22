"""Asset-authoring contract tests: reconstruction, repair, source guards and invalid fixtures."""

import shutil
import tempfile
import unittest
from pathlib import Path

import numpy as np

from HullAssets import (BakePreview, HashFile, HULL_NAMES, ReadCatalog,
                        ReadPreset, ROOT, Validate)
from VoxelAssets import ReadVox, WriteVox


class HullAssetTests(unittest.TestCase):
  def setUp(self):
    self.m_temp = tempfile.TemporaryDirectory()
    self.m_assets = Path(self.m_temp.name) / "Assets"
    (self.m_assets / "Models").mkdir(parents=True)
    shutil.copytree(ROOT / "Assets" / "Hulls", self.m_assets / "Hulls")
    for name in HULL_NAMES:
      for suffix in ("", "Intact"):
        fileName = name + suffix + ".vox"
        shutil.copyfile(ROOT / "Assets" / "Models" / fileName, self.m_assets / "Models" / fileName)

  def tearDown(self):
    self.m_temp.cleanup()

  def Edit(self, _file, _old, _new):
    path = self.m_assets / "Hulls" / _file
    text = path.read_text(encoding="utf-8")
    self.assertIn(_old, text)
    path.write_text(text.replace(_old, _new, 1), encoding="utf-8")

  def Preset(self):
    path = self.m_assets / "Models" / "FrigateIntact.vox"
    master, palette = ReadVox(path)
    return master, palette, ReadPreset(self.m_assets / "Hulls" / "FrigateF317.txt", master,
                                      "Frigate", HashFile(path))

  def testDamageReconstructsTheExactNativePreviewBytes(self):
    master, palette, (header, present, appearance, count) = self.Preset()
    output = Path(self.m_temp.name) / "Preview.vox"
    WriteVox(output, BakePreview(master, present, appearance), palette)
    self.assertEqual(output.read_bytes(), (self.m_assets / "Models" / "Frigate.vox").read_bytes())
    self.assertGreater(count, 0)
    expected, _ = ReadVox(self.m_assets / "Models" / "Frigate.vox")
    self.assertTrue(np.array_equal(present, expected != 0))

  def testRepairRestoresGeometryAndCleanColors(self):
    master, palette, (header, present, appearance, count) = self.Preset()
    output = Path(self.m_temp.name) / "Repaired.vox"
    repaired = BakePreview(master, master != 0, np.zeros_like(appearance))
    WriteVox(output, repaired, palette)
    self.assertEqual(output.read_bytes(), (self.m_assets / "Models" / "FrigateIntact.vox").read_bytes())
    self.assertGreater(np.count_nonzero(appearance), 0)

  def testFiveMastersHaveOneConnectedCoreAndSeverableFrigate(self):
    report = Validate(self.m_assets)
    self.assertEqual(set(report["hulls"]), set(HULL_NAMES))
    self.assertTrue(all(hull["coreConnected"] for hull in report["hulls"].values()))
    self.assertGreater(report["preset"]["midsectionDetachedVoxels"], 1000)

  def testRejectsUnknownOrDuplicateFields(self):
    self.Edit("Hulls.txt", "catalog name=MvpHulls version=1", "catalog name=MvpHulls version=1 extra=1")
    with self.assertRaisesRegex(ValueError, "wrong keys"):
      ReadCatalog(self.m_assets)
    self.Edit("Hulls.txt", "extra=1", "version=1")
    with self.assertRaisesRegex(ValueError, "duplicate key"):
      ReadCatalog(self.m_assets)

  def testRejectsMissingOrMultipleCoreRegions(self):
    self.Edit("Hulls.txt", "region hull=Frigate name=Core function=core",
              "region hull=Frigate name=Core function=power")
    with self.assertRaisesRegex(ValueError, "Expected one core"):
      ReadCatalog(self.m_assets)
    self.Edit("Hulls.txt", "region hull=Frigate name=Core function=power",
              "region hull=Frigate name=Core function=core")
    self.Edit("Hulls.txt", "region hull=Frigate name=Reactor function=power",
              "region hull=Frigate name=Reactor function=core")
    with self.assertRaisesRegex(ValueError, "Expected one core"):
      ReadCatalog(self.m_assets)

  def testRejectsRegionOutsideTheVoxelGrid(self):
    self.Edit("Hulls.txt", "min=22,28,12 max=26,34,16", "min=22,28,12 max=256,34,16")
    with self.assertRaisesRegex(ValueError, "Out-of-bounds region"):
      ReadCatalog(self.m_assets)

  def testRejectsStaleMaster(self):
    path = self.m_assets / "Models" / "FrigateIntact.vox"
    cells, palette = ReadVox(path)
    cells[24, 30, 14] = 9 if cells[24, 30, 14] != 9 else 10
    WriteVox(path, cells, palette)
    with self.assertRaisesRegex(ValueError, "Stale master hash"):
      ReadCatalog(self.m_assets)

  def testRejectsOutOfBoundsAndOverlappingMaskRuns(self):
    path = self.m_assets / "Hulls" / "FrigateF317.txt"
    original = path.read_text(encoding="utf-8")
    first = next(line for line in original.splitlines() if line.startswith("remove "))
    self.Edit("FrigateF317.txt", first, "remove start=999999 count=1")
    with self.assertRaisesRegex(ValueError, "Out-of-bounds"):
      self.Preset()
    path.write_text(original.replace(first, first + "\n" + first, 1), encoding="utf-8")
    with self.assertRaisesRegex(ValueError, "overlapping"):
      self.Preset()

  def testRejectsRemovingEmptyArchetypeCells(self):
    path = self.m_assets / "Hulls" / "FrigateF317.txt"
    first = next(line for line in path.read_text().splitlines() if line.startswith("remove "))
    self.Edit("FrigateF317.txt", first, "remove start=0 count=1")
    with self.assertRaisesRegex(ValueError, "empty archetype"):
      self.Preset()

  def testRejectsStaleAppearanceReference(self):
    path = self.m_assets / "Models" / "Frigate.vox"
    cells, palette = ReadVox(path)
    cells[24, 30, 14] = 9 if cells[24, 30, 14] != 9 else 10
    WriteVox(path, cells, palette)
    with self.assertRaisesRegex(ValueError, "Stale appearance hash"):
      self.Preset()

  def testCosmeticChangesCannotChangePresence(self):
    master, palette, (header, present, appearance, count) = self.Preset()
    source = present.copy()
    baked = BakePreview(master, present, appearance)
    self.assertTrue(np.array_equal(present, source))
    self.assertTrue(np.array_equal(baked != 0, present))


if __name__ == "__main__":
  unittest.main()
