"""Blender preview of damage-ready source geometry, authored damage and inspection cutaways.

blender -b -t 3 --python Tools/PreviewHulls.py -- --hull Frigate --view cutaway --output /tmp/Cutaway.png
Inspection views never alter or save the master VOX. Lighting is an offline approximation.
"""

import argparse
from pathlib import Path
import sys

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from HullAssets import BakePreview, ReadCatalog, ReadPreset, ROOT
from PreviewStation import Materials, MeshObject


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--hull", choices=("Frigate", "EscortLancer", "EscortTalon", "Tug", "Raider"), default="Frigate")
  parser.add_argument("--view", choices=("intact", "damaged", "cutaway", "removed"), default="intact")
  parser.add_argument("--output", type=Path, required=True)
  parser.add_argument("--size", type=int, default=1280)
  parser.add_argument("--samples", type=int, default=24)
  options = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
  hulls, regions, masters, palette = ReadCatalog(ROOT / "Assets")
  cells = masters[options.hull].copy()
  if options.view in ("damaged", "removed"):
    if options.hull != "Frigate":
      raise ValueError("Only the frigate has a starting-damage preset")
    header, present, appearance, count = ReadPreset(ROOT / "Assets/Hulls/FrigateF317.txt", cells,
                                                   "Frigate", hulls["Frigate"]["sha256"])
    cells = BakePreview(cells, present, appearance) if options.view == "damaged" else np.where(
      (cells != 0) & ~present, cells, 0).astype(np.uint8)
  elif options.view == "cutaway":
    cells[cells.shape[0] // 2:, :, :] = 0
  bpy.ops.object.select_all(action="SELECT")
  bpy.ops.object.delete(use_global=False)
  obj = MeshObject(options.hull + " " + options.view, cells, Materials(palette))
  obj.location = -Vector(np.array(cells.shape) / 2)
  size = max(cells.shape)
  camera = bpy.data.cameras.new("Camera")
  camera.type = "ORTHO"
  camera.ortho_scale = size * 1.52
  cameraObj = bpy.data.objects.new("Camera", camera)
  bpy.context.collection.objects.link(cameraObj)
  cameraObj.location = Vector((1.4, -.9, .95)) * size * 2
  cameraObj.rotation_euler = (-cameraObj.location).to_track_quat("-Z", "Y").to_euler()
  scene = bpy.context.scene
  scene.camera = cameraObj
  scene.world.use_nodes = True
  background = scene.world.node_tree.nodes.get("Background")
  background.inputs[0].default_value = (.14, .19, .27, 1)
  background.inputs[1].default_value = .65
  for name, direction, energy, color in (("Key", (-.5, .3, -1), 3.0, (1, .96, .9)),
                                        ("Fill", (.6, -.2, -.4), 1.2, (.5, .7, 1))):
    light = bpy.data.lights.new(name, "SUN")
    light.energy = energy
    light.angle = .12
    light.color = color
    lightObj = bpy.data.objects.new(name, light)
    bpy.context.collection.objects.link(lightObj)
    lightObj.rotation_euler = Vector(direction).to_track_quat("-Z", "Y").to_euler()
  scene.render.engine = "CYCLES"
  scene.cycles.samples = options.samples
  scene.cycles.use_denoising = False
  scene.render.threads_mode = "FIXED"
  scene.render.threads = 3
  scene.render.resolution_x = options.size
  scene.render.resolution_y = round(options.size * 9 / 16)
  scene.render.resolution_percentage = 100
  scene.render.film_transparent = True
  scene.view_settings.view_transform = "AgX"
  scene.use_nodes = True
  nodes = scene.node_tree.nodes
  nodes.clear()
  layer = nodes.new("CompositorNodeRLayers")
  color = nodes.new("CompositorNodeRGB")
  color.outputs[0].default_value = (.006, .010, .018, 1)
  over = nodes.new("CompositorNodeAlphaOver")
  output = nodes.new("CompositorNodeComposite")
  scene.node_tree.links.new(color.outputs[0], over.inputs[1])
  scene.node_tree.links.new(layer.outputs["Image"], over.inputs[2])
  scene.node_tree.links.new(over.outputs[0], output.inputs["Image"])
  options.output.parent.mkdir(parents=True, exist_ok=True)
  scene.render.filepath = str(options.output.resolve())
  bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
  Main()
