# F6 — renderer baseline and crowd benchmark

**Taken:** 2026-11-22, on branch `mvp-foundations` (F0 to F6), before any renderer change. **Verdict: go.** ADR-003's per-voxel technique stays: the 100-player crowd fits the frame at full detail, and with crude distance stand-ins it costs a third of that.

## How it was measured

- **Machine:** RTX 3070 Ti Laptop GPU, driver 610.60, the reference GPU of the implementation plan. The machine was not otherwise loaded. Power plan and GPU clocks were left as they were, not pinned.
- **Build:** Release|x64, built through the solution. The debug layer is off in Release.
- **Command:** `Outpost.exe --benchmark [--assets <scene>] --report <file>`. It renders 600 frames at 1920 × 1080 with v-sync off. The first 60 frames are a warm-up and are not counted, and neither is the last frame, which captures the image and waits for the GPU. That leaves 539 measured frames per run.
- **The shadow map is redrawn every frame**, as moving ships will need (ADR-003 draws it once). So every number below includes the shadow pass.
- **GPU time** comes from timestamp queries `GraphicsDevice` writes at the start and end of every frame. `TacticalClient` adds a timestamp after the shadow pass and another after the scene.
- **Frame interval** is the CPU's wall clock between frames. It tracks the GPU time, so these runs are bound by the GPU.
- **Surface extraction** is the median wall time of five `ExtractSurface` calls per model, on the same Release build.
- **Crowd scenes** were written by `AssetGenerator --crowd <dir>` (`AssetGenerator/CrowdScene.*`) and are not committed. `AssetGenerator` was removed from the tree afterwards (PR #3, which replaced it with editable models); the crowd generator is in the history at commit `fff703f`, so a rerun of this benchmark needs it brought back first.
  - **Load:** GameDesign.md §8.2's load. 600 ships within about 900 m of the centre: 260 Lancers, 260 Talons, 50 tugs and 30 frigates. Around them, 20 outposts, each the sample's 12 station modules and its claim asteroid, on a ring 1.5 to 3.5 km out. 860 instances in all.
  - **Battle:** the camera 300 m from the centre, inside the crowd.
  - **Overview:** the camera 2.4 km out, with the whole crowd in view.
  - **`Lod` variants:** objects whose voxels project to under 3 px are swapped for a 2× downsampled copy, and under 1.5 px for a 4× copy. A downsampled cell is solid when a quarter of its cells are. This is a crude stand-in for per-brick levels of detail, not the real thing.
- **Drawn surface voxels** are summed over instances: each instance's model surface count from the report, times the number of instances of that model in the scene file.

## Results

GPU milliseconds per frame, p50 / p95, from three runs each. The third run was taken after a clean `Rebuild` of Release. The first two came from an incremental build, and incremental builds in this tree were found to keep stale objects after a header changed (see the smoke baseline note); the third run agrees with them within noise. The pass columns are from the first run.

| Scene | Surface voxels drawn per frame | GPU frame, runs 1 / 2 / 3 | Shadow pass | Scene pass | HUD |
|---|---|---|---|---|---|
| Sample shipyard (`Assets/`) | 134,903 | 1.39 / 1.60 · 1.59 / 3.15 · 1.64 / 4.08 | 0.41 / 0.43 | 1.02 / 1.06 | 0.04 |
| Crowd, battle view, full detail | 3,649,230 | 6.30 / 6.32 · 6.29 / 6.32 · 6.30 / 6.32 | 3.08 / 3.11 | 3.20 / 3.21 | 0.02 |
| Crowd, battle view, stand-ins (208 full, 373 at 2×, 279 at 4×) | 1,033,887 | 1.97 / 2.26 · 2.03 / 2.48 · 2.14 / 2.58 | 0.93 / 1.08 | 1.04 / 1.18 | 0.01 |
| Crowd, overview, full detail | 3,649,230 | 6.86 / 7.25 · 7.00 / 7.17 · 7.12 / 7.98 | 3.44 / 3.63 | 3.41 / 3.60 | 0.02 |
| Crowd, overview, stand-ins (1 full, 3 at 2×, 856 at 4×) | 234,223 | 0.61 / 0.62 · 0.66 / 0.68 · 0.66 / 0.67 | 0.25 / 0.26 | 0.35 / 0.35 | 0.01 |

The p50s agree within about 10 % from run to run. The sample's tail is noisy, with a p95 of 1.6 ms in one run and 4.1 ms in another, and the full-detail overview's p95 moved by 0.8 ms; the battle runs did not move. It is most likely the laptop's clocks, which were not pinned. The verdict holds even at the worst p95 seen in any run.

### Surface extraction (CPU, Release)

| Model | Grid | Solid | Surface | Time |
|---|---|---|---|---|
| Frigate | 48 × 150 × 40 | 76,306 | 16,981 | 2.49 to 2.64 ms |
| HangarBay | 64 × 52 × 38 | 49,942 | 22,970 | 2.22 ms |
| CommandBlock | 48 × 40 × 46 | 40,342 | 7,209 | 0.92 ms |
| EscortLancer | 28 × 52 × 18 | 4,230 | 2,118 | 0.14 ms |
| AsteroidMajor | 128 × 128 × 160 | 578,479 | 52,000 | 15.2 to 19.6 ms |
| Solid 16³ brick | 16 × 16 × 16 | 4,096 | 1,352 | 0.070 to 0.096 ms |
| Busiest 16³ brick of any shipped model (HangarBay, origin 0,32,0) | 16 × 16 × 16 | 2,573 | 1,545 | 0.065 to 0.071 ms |

The full table, with every shipped model, is in the benchmark report and repeats when the command above is run.

## What the numbers say

- **Go.** The plan's criterion is a stand-in run that fits about 12 ms at 1920 × 1080 on this GPU, main and shadow pass included. The stand-in runs take 2.0 to 2.1 ms at p50 (battle) and 0.6 to 0.7 ms (overview), and full detail takes 6.3 to 7.1 ms, with 8.0 ms the worst p95 seen. Even without levels of detail, the crowd fits the plan's 16.7 ms frame budget.
- **Cost scales with drawn voxels, and the shadow pass is about half of it.** The battle crowd draws 27 times the sample's voxels and costs 4.5 times the sample's GPU time. The shadow pass costs as much as the main pass, because it draws the same quads.
  - For **ADR-010**, that makes caching static casters, the plan's first fallback in slice 1, the most valuable shadow change.
  - It also says that redrawing the shadow map every frame fits the budget today: 0.41 ms for the sample.
- **Re-extracting a brick costs about 0.07 to 0.10 ms on one core.** Re-extracting a whole frigate costs 2.5 to 2.6 ms, and the claim asteroid 15 to 20 ms. For **ADR-013** this confirms bricks:
  - at 0.1 ms a brick, a 2 ms budget per frame re-extracts about 20 dirty bricks;
  - re-extracting whole models per hit would not survive a battle;
  - the claim asteroid (slice 5) cannot be re-extracted whole, even once a frame.

## What it does not cover

- **No damage.** Carving exposes interior voxels, so a shredded hull draws several times its intact surface (GameDesign.md §8.2). At full detail the crowd has a factor of about 2.4 in hand before it reaches 16.7 ms. With levels of detail, it has well over 5.
- **No effects at the crowd's scale:** engine plumes, muzzle flashes, breaches and more than 16 point lights. These are slice 3 and slice 7 work.
- **Outposts at their sample size.** The design expects outposts to grow to two or three times that, which is 2 to 6 million voxels for 20 outposts against the 1.6 million drawn here.
- **The shadow camera is fitted to the whole 7 km crowd,** so the measured shadow pass has the right cost but useless resolution. Cascades around the camera (ADR-010) change the resolution, not the number of quads.
- **One laptop GPU, one resolution.** No 4K and no low-end GPU, and the GPU clocks were not pinned.
