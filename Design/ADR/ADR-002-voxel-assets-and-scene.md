# ADR-002 — Voxel assets, the canonical palette, VOX-to-world axes and the scene file

**Status:** accepted · **Date:** 2026-11-21

## Context

The sample's models must be genuine MagicaVoxel `.vox` files: editable in MagicaVoxel, produced by a deterministic generator, and committed together with that generator. Every model has to agree on what a palette index means, emission has to come from the renderer rather than the file, and large structures must respect the format's 256-voxel dimension limit. Scene placement — which model goes where, how modules dock, what the HUD reports — must be editable without a rebuild of the code.

## Decision

### The `.vox` subset (`NeuronCore/VoxFile`)

- Read and written by hand; no third-party parser (R14).
- **Reader:** accepts version 150 and walks chunks generically (`id`, content bytes, children bytes). It uses `SIZE`, `XYZI` and `RGBA` and skips every other chunk (`PACK`, `nTRN`, `nGRP`, `nSHP`, `MATL`, `LAYR`, `rOBJ`, …), so a file re-saved by MagicaVoxel still loads.
- **Rejections:** a bad magic number, a truncated chunk (any chunk claiming more bytes than remain), a missing `MAIN`, `SIZE` or `XYZI`, a dimension outside 1..256, and any voxel outside its model's bounds. Each has a test.
- **Writer:** emits `MAIN` → `SIZE`, `XYZI`, `RGBA`, with voxels in z, y, x order, so a model always serializes to the same bytes.

### Palette indexing — the offset, stated once

A voxel's colour index `c` is 1..255, and 0 is empty space. The `RGBA` chunk holds 256 entries in file order, and **voxel colour `c` is chunk entry `c − 1`**. `VoxPalette` is indexed by `c` directly. The shift happens in exactly two functions, `PaletteFromRgbaChunk` and `PaletteToRgbaChunk`, and nowhere else. The chunk's last entry is never addressed by a voxel and is written as zero. A hand-authored fixture in the tests pins this down byte by byte.

### One canonical palette, emission elsewhere

`GameCore/Palette` defines the palette as named families of shades: charcoal hull, gray and off-white armour, safety orange, hazard stripes, rock, steel, decking, glass, scorch, frames, insignia, amber lamps and windows, beacon red, nav green, breach embers and signal cyan. Every generated file embeds **the identical 1024-byte `RGBA` chunk**. A test compares every file in `Assets/Models/` byte for byte against the canonical chunk, and against each other.

Emission is **not** in the file. `MaterialTable()` maps each palette index to an emissive colour and an optional glow-sprite radius. The renderer reads it as a 256-entry material buffer, so a lamp glows because of what its index means, not because of anything stored in the `.vox`. A palette mismatch in a loaded file is reported as a warning; the canonical table still wins.

### Dimension limits and modules

No model exceeds 256 on any axis. The largest are the major asteroid (128 × 128 × 160), the frigate (48 × 150 × 40) and the hangar bay (64 × 52 × 38). The station is built from **modules** — hangar bay, hangar section, command block, tank cluster, docking collar, truss beam, truss tower — placed in the scene file and joined by sockets. The blueprint extension at the docking socket is the `HangarSection` model drawn as a wireframe (ADR-003), so the plan and the built module are the same geometry.

### The generator

`AssetGenerator.exe --out Assets` writes all 19 models from code, with no randomness other than hashes of coordinates and fixed seeds, so the output is byte-identical on every run and in both configurations. `--check` regenerates in memory and fails if any committed file differs. Measured: the Release generator rewrote all 19 files in 0.42 s, wall clock via `Measure-Command`, and its output hashed identical to the committed files. Both the generator and its output are committed.

### VOX to world

VOX is right-handed with +Z up, and ships face +Y. The world is Direct3D: left-handed, +X right, +Y up, +Z forward. For a model with pivot `p` (default: the centre of its grid) and voxel size `s`, a VOX lattice point `v` maps to model space as

```
model = ((v.x − p.x), (v.z − p.z), (v.y − p.y)) · s
```

then through the instance's scale, yaw/pitch/roll (degrees; positive yaw turns +Z toward +X) and position. The swap of y and z is a reflection — the determinant is negative — which is exactly what turns the right-handed file into the left-handed world. Face winding and normals are derived after the transform, not assumed. `NeuronCore/ModelAxes` is the only code that knows this; tests pin the axis mapping, the pivot and scale, the matrix against the point function, and the handedness.

### Sockets

A socket is a named point on a model, in VOX coordinates, with an outward normal and an up vector. Attaching a child instance to a parent's socket finds the child transform that makes the two socket origins coincide, the normals face each other, and the up vectors agree (`MateSockets`). The tug carries its cargo module this way, and the blueprint extension sits on the hangar's `ExtensionDock` socket. Tests check the mating algebra on a rotated parent, and that the shipped scene's extension really lands on the dock.

### The scene file

`Assets/Scene.txt` is a line-oriented record file: `keyword key=value …`, `#` comments, and quoted values for text with spaces. It is parsed by `NeuronCore/RecordFile`, and `GameCore/SceneDefinition` interprets the records:

| Keyword | Defines |
|---|---|
| `camera` | Initial orbit target, yaw, pitch, distance, field of view; R / Home returns to it |
| `keylight`, `stars` | Key light direction, colour and ambient; star seed and count |
| `model`, `socket` | A `.vox` file with optional voxel size and pivot; named sockets on it |
| `instance` | A model placed in the world, or attached by `parent=`/`parentsocket=`/`socket=`; `style=blueprint` draws it as a wireframe |
| `ship` | Gameplay data for an instance: callsign, class, hull and per-module health (drives the selected-ship panel) |
| `engine`, `light` | Engine plumes and amber point lights, positioned in the owning model's VOX coordinates |
| `objective`, `resource`, `build`, `select` | HUD content: objectives, resources and their rates, build-panel thumbnails, the initial selection |

Unknown keywords and malformed values fail with the line number. `ValidateScene` then checks every reference — model files, models, parents, sockets, ships, engines, lights. The executable refuses to start on a broken scene (exit code 3), and a test runs validation over the shipped scene.

## Consequences

- Editing a model by hand in MagicaVoxel is supported, but the next `AssetGenerator --out Assets` overwrites it. A hand-edited model should get its own file name and its own `model` line.
- A new palette role is a change to `GameCore/Palette` plus regenerating every model, and the palette test fails until that is done.
- Anything larger than 256 voxels on an axis must be built from modules and sockets; the format leaves no alternative, and the scene file is designed around it.
