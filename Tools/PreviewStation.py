"""Render actual VOX assets offline with Blender; this is not a Direct3D game capture.

blender -b -t 3 --python Tools/PreviewStation.py -- --assets Assets --output /tmp/Station.png
Add --model HangarBay for an individual kit component, --size 1920 --samples 32 for quality.
The script reads Scene.txt, including pivots, voxel sizes, rotations and socket attachments.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import bpy
import numpy as np
from mathutils import Matrix, Vector

from VoxelAssets import InstanceMatrices, MeshFaces, ParseScene, ReadVox, SWIZZLE, Triple, STROBE_YELLOW, WINDOW_BLUE


def Materials(_palette):
  palette = np.frombuffer(_palette, np.uint8).reshape(256, 4)
  materials = []
  for index in range(256):
    rgb = palette[index - 1, :3].astype(float) / 255 if index else np.zeros(3)
    linear = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    material = bpy.data.materials.new(f"Palette {index:03}")
    material.diffuse_color = (*linear, 1)
    material.use_nodes = True
    surface = material.node_tree.nodes.get("Principled BSDF")
    surface.inputs["Base Color"].default_value = (*linear, 1)
    surface.inputs["Roughness"].default_value = 0.62
    surface.inputs["Metallic"].default_value = 0.25 if index < 97 else 0.1
    if index == WINDOW_BLUE:
      surface.inputs["Metallic"].default_value = 0
      surface.inputs["Roughness"].default_value = 0.05
      surface.inputs["Transmission Weight"].default_value = 0.68627451
      surface.inputs["IOR"].default_value = 1.3
    if 200 <= index <= STROBE_YELLOW:
      strength = 2.6 if index <= 207 else 3.2
      if index == STROBE_YELLOW:
        strength = 10
      surface.inputs["Emission Color"].default_value = (*linear, 1)
      surface.inputs["Emission Strength"].default_value = strength
    materials.append(material)
  return materials


def MeshObject(_name, _cells, _materials):
  vertices, faces, colors = MeshFaces(_cells)
  mesh = bpy.data.meshes.new(_name)
  mesh.from_pydata(vertices, [], faces)
  mesh.update()
  for material in _materials:
    mesh.materials.append(material)
  mesh.polygons.foreach_set("material_index", colors)
  obj = bpy.data.objects.new(_name, mesh)
  bpy.context.collection.objects.link(obj)
  print(f"Mesh {_name}: {len(faces):,} exposed quads", flush=True)
  return obj


def Camera(_location, _target, _fov):
  camera = bpy.data.cameras.new("Camera")
  camera.lens_unit = "FOV"
  # Blender's horizontal sensor fit means FOV is horizontal at landscape aspect.
  camera.angle = math.radians(_fov)
  camera.clip_end = 5000
  obj = bpy.data.objects.new("Camera", camera)
  bpy.context.collection.objects.link(obj)
  obj.location = Vector(_location)
  obj.rotation_euler = (Vector(_target) - obj.location).to_track_quat("-Z", "Y").to_euler()
  bpy.context.scene.camera = obj
  return obj


def Main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--assets", type=Path, default=Path("Assets"))
  parser.add_argument("--output", type=Path, required=True)
  parser.add_argument("--model")
  parser.add_argument("--size", type=int, default=1600)
  parser.add_argument("--samples", type=int, default=24)
  parser.add_argument("--view", choices=("scene", "station", "reverse"), default="scene")
  options = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
  bpy.ops.object.select_all(action="SELECT")
  bpy.ops.object.delete(use_global=False)
  records = ParseScene(options.assets / "Scene.txt")
  models = {record["name"]: record for record in records["model"]}
  grids = {}
  for name, model in models.items():
    cells, palette = ReadVox(options.assets / model["file"])
    grids[name] = cells
  materials = Materials(palette)
  objects = {}
  if options.model:
    cells = grids[options.model]
    obj = MeshObject(options.model, cells, materials)
    center = np.array(cells.shape) / 2
    obj.location = -Vector(center)
    for instance in records["instance"]:
      if instance["model"] == options.model and instance.get("style") != "blueprint":
        objects[instance["name"]] = obj
        break
    radius = max(cells.shape)
    Camera((radius * 1.40, -radius * 2.40, radius * 1.50), (0, 0, 0), 42)
  else:
    matrices = InstanceMatrices(records, grids)
    sources = {}
    bounds = []
    for instance in records["instance"]:
      name, modelName = instance["name"], instance["model"]
      isStation = modelName in {"HangarBay", "HangarSection", "CommandBlock", "TankCluster", "DockingCollar",
                              "TrussBeam", "TrussTower", "Habitat", "Reactor", "Refinery", "TurretPlatform",
                              "ShieldGenerator", "Yard", "RadiatorArray"}
      if options.view != "scene" and not isStation:
        continue
      if instance.get("style") == "blueprint":
        # Runtime uses coarse occupancy wireframes; preview uses an explicitly simplified envelope.
        size = grids[modelName].shape
        bpy.ops.mesh.primitive_cube_add(size=1)
        obj = bpy.context.object
        obj.name = "Blueprint envelope (preview only)"
        for vertex in obj.data.vertices:
          vertex.co = Vector(((vertex.co.x + .5) * size[0], (vertex.co.y + .5) * size[1],
                              (vertex.co.z + .5) * size[2]))
        obj.data.materials.append(materials[220])
        wire = obj.modifiers.new("Blueprint outline", "WIREFRAME")
        wire.thickness = 0.28
      else:
        if modelName not in sources:
          sources[modelName] = MeshObject(modelName, grids[modelName], materials)
          obj = sources[modelName]
        else:
          obj = bpy.data.objects.new(name, sources[modelName].data)
          bpy.context.collection.objects.link(obj)
      model = models[modelName]
      pivot = Triple(model["pivot"]) if "pivot" in model else np.array(grids[modelName].shape) / 2
      voxToModel = np.eye(4)
      voxToModel[:3, :3] *= float(model.get("voxelsize", 1))
      voxToModel[:3, 3] = -pivot * float(model.get("voxelsize", 1))
      matrix = SWIZZLE @ matrices[name] @ SWIZZLE @ voxToModel
      obj.matrix_world = Matrix(matrix.tolist())
      obj.name = name
      obj["sceneInstance"] = name
      objects[name] = obj
      if isStation:
        bounds.extend(obj.matrix_world @ Vector(corner) for corner in obj.bound_box)
    camera = records["camera"][0]
    target = Triple(camera["target"])[[0, 2, 1]]
    yaw, pitch = math.radians(float(camera["yaw"])), math.radians(float(camera["pitch"]))
    distance = float(camera["distance"])
    direction = np.array((math.sin(yaw) * math.cos(pitch), math.cos(yaw) * math.cos(pitch), -math.sin(pitch)))
    fovY = math.radians(float(camera["fov"]))
    fovX = math.degrees(2 * math.atan(math.tan(fovY / 2) * 16 / 9))
    if options.view == "scene":
      Camera(target - direction * distance, target, fovX)
    else:
      bounds = np.array(bounds)
      target = (bounds.min(axis=0) + bounds.max(axis=0)) / 2
      radius = max(bounds.max(axis=0) - bounds.min(axis=0))
      direction = np.array((0.25, -1, 0.65)) if options.view == "station" else np.array((-0.65, 1, .65))
      Camera(target + direction / np.linalg.norm(direction) * radius * 1.35, target, 51)
  bpy.context.view_layer.update()
  for index, light in enumerate(records.get("light", [])):
    if light.get("instance") not in objects:
      continue
    owner = objects[light["instance"]]
    position = owner.matrix_world @ Vector(Triple(light["position"]))
    radius = float(light["radius"])
    if options.model:
      radius /= float(models[options.model].get("voxelsize", 1))
    data = bpy.data.lights.new(f"Scene point light {index}", "POINT")
    data.energy = float(light["intensity"]) * radius * radius * 2.5
    data.color = Triple(light["color"])
    data.shadow_soft_size = 1.0
    obj = bpy.data.objects.new(data.name, data)
    bpy.context.collection.objects.link(obj)
    obj.location = position
  # Lighting intentionally simple and constant for comparisons.
  scene = bpy.context.scene
  scene.world.use_nodes = True
  scene.world.node_tree.nodes.get("Background").inputs[0].default_value = (0.13, 0.18, 0.28, 1)
  scene.world.node_tree.nodes.get("Background").inputs[1].default_value = 0.36
  sun = bpy.data.lights.new("Key", "SUN")
  sun.energy, sun.angle = 3.0, 0.09
  sunObj = bpy.data.objects.new("Key", sun)
  bpy.context.collection.objects.link(sunObj)
  sunObj.rotation_euler = Vector((-.35, .48, -.80)).to_track_quat("-Z", "Y").to_euler()
  # A restrained broad fill exposes silhouette detail without flattening recesses.
  fill = bpy.data.lights.new("Cool fill", "SUN")
  fill.energy, fill.angle, fill.color = .55, .35, (.40, .65, 1.0)
  fillObj = bpy.data.objects.new("Cool fill", fill)
  bpy.context.collection.objects.link(fillObj)
  fillObj.rotation_euler = Vector((.7, -.8, -.3)).to_track_quat("-Z", "Y").to_euler()
  scene.render.engine = "CYCLES"
  scene.cycles.samples = options.samples
  # Some headless Blender distributions omit OpenImageDenoise entirely.
  scene.cycles.use_denoising = False
  scene.render.threads_mode = "FIXED"
  scene.render.threads = 3
  scene.render.resolution_x, scene.render.resolution_y = options.size, round(options.size * 9 / 16)
  scene.render.resolution_percentage = 100
  scene.render.image_settings.file_format = "PNG"
  scene.render.film_transparent = True
  scene.view_settings.view_transform = "AgX"
  scene.use_nodes = True
  nodes = scene.node_tree.nodes
  nodes.clear()
  layer = nodes.new("CompositorNodeRLayers")
  glow = nodes.new("CompositorNodeGlare")
  glow.glare_type, glow.quality, glow.threshold = "FOG_GLOW", "MEDIUM", 2.5
  background = nodes.new("CompositorNodeRGB")
  background.outputs[0].default_value = (0.006, 0.010, 0.018, 1)
  over = nodes.new("CompositorNodeAlphaOver")
  scene.node_tree.links.new(layer.outputs["Image"], glow.inputs["Image"])
  scene.node_tree.links.new(background.outputs[0], over.inputs[1])
  scene.node_tree.links.new(glow.outputs[0], over.inputs[2])
  output = nodes.new("CompositorNodeComposite")
  scene.node_tree.links.new(over.outputs[0], output.inputs["Image"])
  options.output.parent.mkdir(parents=True, exist_ok=True)
  scene.render.filepath = str(options.output.resolve())
  bpy.ops.wm.save_as_mainfile(filepath=str(options.output.with_suffix(".blend").resolve()))
  bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
  Main()
