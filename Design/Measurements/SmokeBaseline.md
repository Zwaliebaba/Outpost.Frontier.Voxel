# Smoke-test baseline

**Taken:** 2026-11-22, first at commit `381bbc5` (F1 to F3 done), and stored again after F4 for the reason given below. **Purpose:** the reference that every change that must not alter the picture is checked against.

## How it was taken

Debug|x64, built through the solution, the default smoke timeline (3 frames, 1920 × 1080, debug layer on):

```powershell
.\x64\Debug\Outpost.exe --smoke-test --warp --capture SmokeBaselineWarp.png --report SmokeBaselineWarp.txt
.\x64\Debug\Outpost.exe --smoke-test               --report SmokeBaselineGpu.txt
```

The machine-specific lines (`assets:`, `captured:`) were removed from the stored reports.

| File | What it is |
|---|---|
| `SmokeBaselineWarp.png` | The WARP capture of the last frame |
| `SmokeBaselineWarp.txt` | The WARP smoke report |
| `SmokeBaselineGpu.txt` | The same run on the RTX 3070 Ti Laptop GPU, driver 610.60 |

## What was found

- **WARP is byte-deterministic.** Three WARP runs wrote byte-identical PNGs (`cmp`), and each scored a composition similarity of 1.000 against the first. That makes the WARP capture an exact reference, not only a statistical one.
- **The GPU is deterministic too, but differs from WARP.** Two hardware runs wrote byte-identical PNGs; against the WARP capture they score 0.999, with 660 distinct colours against WARP's 659. Compare hardware with the hardware report and WARP with the WARP report.
- Image statistics, identical on both adapters except the colour count: mean luminance 0.146, lit 26.8 %, cyan 1.92 %, warm 2.63 %.
- ADR-003 recorded 658 distinct colours on WARP after two frames; the default timeline is now three frames, and 659 is what it gives.

## A trap found while using it

After `NeuronClient/GraphicsDevice.h` gained members, an incremental Debug build linked objects that had not been recompiled against the new layout. The WARP capture then differed from this reference in 159 pixels, by up to 75 in one channel, spread across the frame. A worktree build of the same sources showed it more bluntly: an incremental build there crashed, and a `Rebuild` of the same sources ran clean and matched. A clean `Rebuild` of this tree matched the reference exactly, with 0 pixels differing. **Before trusting a capture after a header changed its class layout, rebuild with `-t:Rebuild`.** The cause in MSBuild's dependency tracking was not found.

## Why it was stored again after F4

F4 made the client draw only what the server's observer told it, at the poses it sent (AGENTS.md R18, R25). A pose is a double-precision position and a float quaternion, decomposed by the server from the scene's model-to-world matrix and rebuilt into a matrix by the client. The rebuilt matrix differs from the original by float rounding. Against the first reference, 104,859 pixels (5 % of the frame) then differed, by up to 251 in one channel, spread over the whole frame, with the image statistics unchanged except the colour count (657 against 659). The two captures cannot be told apart by eye.

The difference was traced rather than assumed. With the client drawing from the scene's own matrix instead of the received pose, and every other part of F4 unchanged, the capture matched the first reference exactly, with 0 pixels differing. So the messages carry the world exactly, and the whole difference is the pose round trip: voxel edges and shadow-map samples landing a fraction of an ulp elsewhere. The client keeps drawing at the received pose, since placing objects from its own copy of the scene is what F4 removed, and the reference was stored again from that build. The WARP capture now has 657 distinct colours; the GPU report still reads 659.

## How to check against it

```powershell
.\x64\Debug\Outpost.exe --smoke-test --warp --compare Design\Measurements\SmokeBaselineWarp.png --expect Design\Measurements\SmokeBaselineWarp.txt
```

`--expect` fails the run with exit code 9 when the image statistics leave the stored report's tolerance: 0.005 of mean luminance, 0.3 points of the lit, cyan and warm shares, and 5 % of the colour count (`Neuron::StatisticsTolerance`). `--compare` reports both the composition similarity and an exact count of differing pixels with their bounding box. For a change that leaves the picture alone on WARP, it should read 1.000 and 0 pixels.

## Release builds

The references are Debug captures, and a Release build does not reproduce them pixel for pixel.

- **Measured on 2026-11-22.** A Release WARP capture differed from `SmokeBaselineWarp.png` in 42 pixels, all on edges. It matched `SmokeBaselineWarp.txt` under `--expect`, and two Release runs were identical to each other.
- **It predates this branch.** The same comparison at `381bbc5`, before F4, gave 159 differing pixels. It was made between that commit's own Debug and Release captures, counted with a standard-library PNG decoder, which also reproduced the 42 above.
- **The cause is optimisation-dependent floating-point contraction** under `/arch:AVX2`, which AGENTS.md R16 names.

So check a Release build with `--expect`, and use `--compare` against a capture from the same configuration.

## The scripted reference (plan task F5)

`ScriptedSmoke.txt` drives the executable through input at fixed ticks: it clicks the fighter on the right, orbits the view by dragging for 20 ticks, and zooms one wheel step. The run lasts until one frame after the last input, 31 frames here.

```powershell
.d\Debug\Outpost.exe --smoke-test --warp --script Design\Measurements\ScriptedSmoke.txt --expect Design\Measurements\ScriptedSmokeWarp.txt
```

| File | What it is |
|---|---|
| `ScriptedSmoke.txt` | The script; its format is documented in `GameApp/InputScript.h` |
| `ScriptedSmokeWarp.png` | The WARP capture of its last frame |
| `ScriptedSmokeWarp.txt` | Its WARP report, with the machine-specific lines removed |

What was checked when it was stored:

- The report names the selection: `selected: Talon`, the fighter's instance, shown as Escort E-208.
- Two WARP runs wrote byte-identical PNGs.
- The run matches its own report, exit code 0.
- Against the unscripted report it fails with exit code 9, naming the luminance, the lit share and the colour count.
- A `--frames` shorter than the script is refused with exit code 1.
- The RTX 3070 Ti Laptop GPU run matches the WARP report within the tolerance: 628 distinct colours against WARP's 623, the rest identical. So the WARP report also serves as the hardware reference.
