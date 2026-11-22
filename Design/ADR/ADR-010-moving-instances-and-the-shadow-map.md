# ADR-010 — Moving instances and the shadow map

**Status:** accepted · **Date:** 2026-11-22

## Context

Slice 1 makes ships move (plan tasks 1.7 and 1.10). Until now the client drew a still picture:

- **Transforms were computed once**, in `TacticalScene::Build`, from the poses the observer sent first.
- **The shadow map was drawn once**, over a shadow camera fitted to the whole static scene (ADR-003).
- **Every matrix was in world space as floats.** ADR-007 decided double-precision world positions and camera-relative rendering, and left building them to the slice where positions first move.

Three questions had to be settled before anything flew:

1. Where an instance is drawn on a frame that falls between two ticks.
2. What a float matrix is relative to.
3. How the shadow map keeps up with ships that move, and whether that fits the frame.

F6 had already measured a shadow map redrawn every frame on the old station: 0.41 ms of a 1.39 to 1.64 ms frame ([F6-RendererBaseline.md](../Measurements/F6-RendererBaseline.md)). It also named the fallbacks for when that stops fitting, in order: cache the static casters, lower a dynamic cascade's resolution, and shadow moving ships only near the camera.

## Decision

### Where an instance is drawn

- **Every instance is placed from a pose on every frame.** Bounds, picking, labels and engine plumes are built from the same placement, so they follow their ship.
- **The client's own ship is drawn from its prediction** (`GameApp/Prediction`), blended between the last two predicted ticks by the clock's interpolation factor. The prediction runs `StepShip`, the server's own function, so in loopback it matches the server bit for bit.
- **Every other object is drawn one tick in the past**, blended between the last two poses the observer sent (`PoseInterpolation`), at `tick − 1 + alpha`. It is never extrapolated, so it never has to be pulled back. The cost is one tick of delay, 16.7 ms.
- **Blends are done in double precision**, before anything becomes a float.
- **Plumes scale with the ship's motion state**, which the observer sends as a newest-wins datagram. They idle at 0.2 of their length, reach 1.6 under the thruster and 1.3 times their length in cruise, and are not drawn when the engine is off.

### Render space

- **Everything is drawn relative to a render origin**: the point of a 1024 m grid nearest the camera (`TacticalScene::RenderOriginFor`). A world position minus the origin is taken in double precision and only then becomes a float.
- **The origin moves only when the camera crosses half a cell.** So it does not move every frame, and what depends on it is rebuilt only then: the blueprint lines (wireframes of the planned modules).
- **Anything near the camera is within 512 m of the origin** on each axis. At 512 m a float's step is 2⁻¹⁴ m, about 0.06 mm. This holds anywhere in the system: `TacticalSceneTests` checks it at 1,000 km out, and checks that a millimetre's motion 100 km out is still a millimetre in render space.
- **The chase camera works at the origin of its own space** (`ChaseCamera::View`) and is translated into render space, so the flight view has no float world position anywhere.

### The shadow map

- **It is redrawn every frame.** It is one orthographic cascade along the key light, fitted to the bounds of every drawn instance within 1500 m of the camera. Blueprints are excluded because they cast no shadow.
- **It is built to become cascades, not to be replaced by them.** A further cascade is a second fit over a farther range, drawn into a second map or a slice of one; the first does not change. GameDesign.md §8.2 needs that when a system holds hundreds of ships.
- **No caster is cached.** The measurements below show the plain redraw fits with room to spare, so the first fallback waits until a scene needs it.

## Measurements

Taken on 2026-11-22 on the RTX 3070 Ti Laptop GPU with driver 610.60, in Release x64 after a clean `Rebuild`. Each command ran three times:

```powershell
.\x64\Release\Outpost.exe --benchmark --report bench.txt                                                # parked, the flight view
.\x64\Release\Outpost.exe --benchmark --script Design\Measurements\FlightSmoke.txt --report bench.txt   # flying, then ordered
.\x64\Release\Outpost.exe --benchmark --script tactical.txt --report bench.txt                          # tactical.txt: press tick=1 name=F9
```

The benchmark renders 600 frames at 1920 × 1080 with v-sync off. The first 60 frames and the last are not counted, which leaves 539 per run. The scene is the merged station: 53 instances and 623,094 surface voxels, against the 134,903 of the station F6 measured.

GPU milliseconds per frame, p50 / p95:

| View | Frame, runs 1 / 2 / 3 | Shadow pass, runs 1 / 2 / 3 | Scene pass, runs 1 / 2 / 3 |
|---|---|---|---|
| Flight, parked | 2.28 / 2.32 · 2.52 / 2.70 · 2.65 / 3.41 | 0.88 / 0.90 · 0.98 / 1.11 · 1.09 / 1.40 | 1.42 / 1.45 · 1.56 / 1.60 · 1.59 / 2.01 |
| Flight, flying the script | 2.27 / 2.97 · 2.79 / 4.03 · 2.82 / 4.36 | 0.85 / 1.03 · 0.93 / 1.36 · 0.94 / 1.45 | 1.45 / 1.88 · 1.77 / 2.55 · 1.80 / 2.78 |
| Tactical overview | 3.77 / 5.35 · 4.12 / 5.53 · 3.97 / 9.69 | 1.29 / 1.71 · 1.40 / 1.72 · 1.35 / 2.80 | 2.37 / 3.56 · 2.60 / 3.65 · 2.51 / 6.66 |

- **The budget holds by a wide margin.** The worst p50 is 4.1 ms of a 16.7 ms frame. The worst single frame in any run was 16.3 ms, in the third tactical run, whose p99 was 16.0 ms.
- **The tails are the laptop, not the scene.** That third tactical run drew the same frames as the first two, whose p99s were 5.5 ms. Within each view, p50s rose from run to run as the machine warmed. The clocks were not pinned, as they were not for F6.
- **The shadow pass is about 40 % of the frame**, as F6 found (0.41 of 1.39 ms then). Caching static casters remains the most valuable fallback when one is needed.
- **Moving the ship costs nothing measurable.** Flying the script and sitting parked give the same p50 in the first runs, 2.27 and 2.28 ms. Building every placement each frame does not show in the GPU numbers.

Not measured: the shadow map's texel size at the cascade's typical radius, and the crowd scenes of F6, whose generator left the tree with `AssetGenerator`.

## What this forecloses

- **Extrapolating other objects.** They are drawn a tick behind, by interpolation only, and a renderer that wants them at the present must predict them itself.
- **Float world positions in the client's drawing.** A new draw path takes a render-space transform from `TacticalScene`. The tactical view's orbit camera still keeps its position as a float in world space, which is fine near the yard. It must move to render space before the tactical view is used far from the origin, in slice 2, where it becomes the docked view.
- **A shadow map drawn once.** Anything that moves can cast, so nothing assumes the map is still from one frame to the next.

Left open:

- **A correction from the server snaps the own ship** to the replayed state. The design does not yet say how a misprediction should look, because in loopback none happens. It matters once there is a network (slice 10).
