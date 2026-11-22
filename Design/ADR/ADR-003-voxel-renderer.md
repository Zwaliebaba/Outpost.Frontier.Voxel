# ADR-003 — The voxel renderer, frame structure and verification

**Status:** accepted · **Date:** 2026-11-21

## Context

The sample draws about 122,000 surface voxels: the shipyard, a damaged frigate, escorts, a tug, an asteroid field and debris. On top of that it adds lighting, emissive glow, engine plumes, a depth-tested blueprint wireframe and a HUD that scales with the window. Direct3D 12 is fixed (R12), with no helper libraries (R14), and it has to run on a laptop GPU and on WARP for an automated smoke test. AGENTS.md §5 leaves the renderer's shape to an ADR; this is it.

## Decision

### Voxels: one screen-space quad per surface voxel, ray-traced against its box

Following Majercik, Crassin, Shirley and McGuire, *A Ray-Box Intersection Algorithm and Efficient Dynamic Voxel Rendering* (JCGT 2018; `Design/Majercik2018Voxel.pdf`):

- **Surface extraction.** `ExtractSurface` keeps only voxels with at least one exposed face. Each goes to the GPU as 8 bytes: position, colour index, exposed-face mask, and 2-bit per-face occlusion for ambient occlusion. Enclosed voxels are never uploaded.
- **Vertex stage.** `DrawInstanced(4, voxelCount)` draws a triangle strip per voxel, with no vertex buffer. The vertex shader bounds the voxel's sphere with its screen-space quadric. When that bound exceeds 12 pixels it switches to the tighter projection of the eight box corners.
- **Culling in the vertex stage.** It culls voxels behind the near plane. It rejects a voxel when none of its exposed faces points toward the camera. Below 0.35 px² of coverage, voxels are pruned stochastically with a stable per-voxel hash, so distant rock does not shimmer.
- **Depth.** The quad is placed at the sphere's nearest depth.
- **Pixel stage.** The pixel shader casts the view ray in the voxel's own space against the unit box (the paper's Listing 5). It shades the face it hits and writes the true depth through `SV_DepthGreaterEqual`. Because the quad is never behind the true surface, early depth rejection survives. That conservative-depth output requires `SV_Position` to be `linear noperspective centroid`.
- **Shadows.** The same quads draw a 4096² `D32_FLOAT` orthographic shadow map from the key light. The scene is static, so it is drawn once and kept; the shading pass filters it with 3 × 3 PCF.

### Lighting and colour

- **Lights:** a key light with shadows, hemisphere ambient, up to 16 point lights per frame, and per-voxel shading.
  - The point lights are amber hangar and breach lights from the scene file, plus engine glows.
  - Per-voxel shading adds face ambient occlusion, an edge bevel, and small colour variation per voxel.
- **Emission:** comes from the material table (ADR-002). Lamps with a glow radius also get an additive glow sprite.
- **Engine plumes:** additive, view-facing beams aligned to each engine's axis.
- **Colour pipeline:** shading happens in linear light, then exposure and Narkowicz's ACES fit. The output goes to an `R8G8B8A8_UNORM_SRGB` view of an `R8G8B8A8_UNORM` swap chain, so the hardware applies the sRGB curve on write.

### Lines and the blueprint

World-space segments are expanded to screen-space quads in the vertex shader, blended additively and **depth-tested** without depth writes, so the station hides the parts of the wireframe behind it. The blueprint extension draws a wide faint pass under a thin bright one.

Its geometry comes from the same `HangarSection` model as the built module. The model is first coarsened into 4-voxel blocks that count as solid at 30% occupancy. The creases of that coarse surface are drawn, with a lattice line every three blocks on flat runs, and scaled back to the model's coordinates. Wiring the full-resolution model drew every greeble and read as noise, not as a plan.

The selected-ship panel uses a side elevation of the same voxels: the outline of the model's projection onto the VOX y–z plane, plus lines where the nearest surface steps by three voxels or more.

### HUD

The HUD is batched on the CPU (`HudBatch`) as pixel-space triangles, pulled from a structured buffer and alpha-blended with depth off.
- **Coverage:** text and shapes share one R8 atlas; a white block gives solid shapes full coverage.
- **Fonts:** glyphs are rasterized once by GDI, as Bahnschrift with a Segoe UI fallback. The atlas is rebuilt only when the HUD scale changes.
- **Scale:** the HUD scale is the window height / 1080, clamped to 0.6–2.0 and quantized to 0.05.
- **Linear blending:** colours are authored in sRGB and converted to linear before blending. Because blending happens in linear light, a panel that should hide the scene needs an alpha of about 0.99; at 0.96 a lit rock behind it was still clearly visible.
- **Thumbnails and silhouette:** build-panel thumbnails and the selected-ship silhouette are drawn into their own viewports between the HUD's back and front layers.

### Frame structure and device lifetime

- **Pipeline state:** one root signature serves every pipeline. It holds a frame CBV, a draw CBV, two root SRVs, and a pixel-only table for the shadow map and the HUD atlas, plus a comparison sampler and a linear clamp sampler.
- **Buffering:** three back buffers, each with its own command allocator and a 48 MB upload ring for per-frame constants and dynamic geometry. A back buffer is reused only after the fence value its last frame signalled has completed.
- **Resize:** waits for idle, releases the size-dependent targets, calls `ResizeBuffers`, and recreates the depth buffer. A minimised window, at 0 × 0, renders nothing and sleeps in `WaitMessage`.
- **Shutdown:** drains the GPU twice — in `WM_CLOSE`, before the window is destroyed, and in `~TacticalClient`, before any client-owned resource is released. Without this, a resize followed by close intermittently stalled the process in a resource release, because the final frame was still in flight. That reproduced about one run in three, and 0 of 10 after the fix.
- **Diagnostics:** in Debug the D3D12 debug layer is on by default (`--no-debug-layer` turns it off). An `ID3D12InfoQueue1` callback counts errors and warnings and keeps their text for the report.

### Verification: the smoke test

`Outpost.exe --smoke-test [--warp]` renders a fixed timeline: frame *i* is at *i*/60 s and advances the server one tick. It then captures the back buffer and checks the image is lit, varied, and carries both the cyan HUD and the amber lights. It fails on any debug-layer error. `--compare <png>` reports a 48 × 27-cell composition similarity against a reference image; it is informational, not a gate.

Exit codes: 0 success, 1 usage, 2 device (no FL 12_1 / SM 6.7 adapter), 3 scene, 4 render, 5 debug-layer errors, 6 image check, 7 capture.

**Measured**, from the smoke report at 1920 × 1080 after two frames on WARP, identical in Debug and Release:
- 38 instances, 19 models, 122,142 surface voxels;
- mean luminance 0.146, lit 26.8%, cyan 1.92%, warm 2.63%, 658 distinct colours;
- composition similarity 0.794 against `Design/Space Station HUD Without Navigation Path.png`.

The interactive Debug build on an RTX 3070 Ti Laptop GPU showed 165 fps on its status line, the display's v-sync limit, so no uncapped frame time has been measured.

## Consequences

- Fill cost scales with screen coverage, not triangle count, and the pixel shader runs once per covered pixel per voxel quad. Very close voxels are the expensive case; the 12-pixel switch to box bounds keeps their quads tight.
- The shadow map is drawn once, so anything that moves will need it redrawn; live destruction and moving ships are out of scope.
- *Added with the design ([GameDesign.md](../GameDesign.md)):* that scope has ended. Ships move from the design's slice 1, and voxels are removed and restored from slice 3 (§7.1), so the surface extracted once and the shadow map drawn once both have to change. How is a new ADR, due before slice 3 (§9); this one stands for the static scene.
- The renderer knows nothing about ships, stations or palette roles. The game supplies the material table and decides what to draw, which keeps R9's split.
- The smoke test is a pixel-statistics gate, not a golden-image diff: it catches a blank, black, unlit or HUD-less frame and debug-layer errors, not a subtle shading regression.
