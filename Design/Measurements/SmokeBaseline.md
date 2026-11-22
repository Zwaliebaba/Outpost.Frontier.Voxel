# Smoke-test baseline

**Taken:** 2026-11-22, first at commit `381bbc5` (F1 to F3 done), and stored again after F4, after the new station and after slice 1, for the reasons given below. **Purpose:** the reference that every change that must not alter the picture is checked against.

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

## Why it was stored again after merging the new station

PR #3 replaced the station on `main`: 27 editable models in place of the generated 19, and a new `Scene.txt`, with 53 instances where there were 38. Every reference here shows the scene it was taken from, so all of them were stored again from the merged tree. Before that, the merged build was checked against a capture from `origin/main`'s own build, `491f2f7`:

- **The image statistics agree.** The mean luminance is 0.135, the lit share 21.8 %, cyan 2.16 % and warm 0.99 %, with 725 distinct colours against `main`'s 726. The composition similarity is 1.000.
- **58,479 pixels differ, all on edges.** That is the F4 pose round trip above, now on the new scene.

What the new references hold:

- **WARP:** three runs wrote byte-identical PNGs. The report reads 53 objects, a first publish of 66 messages, `selected: Frigate`, and 0 debug-layer errors.
- **Hardware:** the RTX 3070 Ti Laptop GPU report matches the WARP one under `--expect`. It reads lit 21.9 % against 21.8 %, cyan 2.15 % against 2.16 %, and the same 725 colours.

## Why it was stored again after slice 1

Slice 1 changed what the default timeline shows, not only how it is drawn:

- **The client starts in the flight view**, behind the player's escort E-114 (the Lancer). The unscripted references are now that chase view, with the flight HUD. They cannot be compared with the tactical frame they replace.
- **Engine plumes scale with each ship's motion state** (ADR-010), so an idle ship's plume is a fifth of its old length.

What the new references hold:

- **WARP:** three runs wrote byte-identical PNGs. The report reads `view: flight`, a first publish of 69 messages where there were 66 (new: a motion state for each of the two escorts, and the own ship's exact state), and a prediction that matched the server on all 3 ticks. The statistics are mean luminance 0.093, lit 10.4 %, cyan 0.63 % and warm 0.25 %, with 414 distinct colours.
- **Hardware:** the RTX 3070 Ti Laptop GPU agrees with WARP on every statistic but the colour count, 411 against 414.
- **The image check's colour floor** is 200 in the flight view and stays 400 in the tactical one. A chase view shows part of the yard against open space.

## Why it was stored again after the signal and glazing materials

PR #5 (ADR-022) added yellow strobes and blue glazing to the models and changed the voxel and effect shaders, without storing the references again. The default run then failed `--expect` with 496 distinct colours against 414. All three references were stored again from `main` at `0e95f20`, after checking where the frames changed:

- **Default:** 10,465 pixels differ from the slice 1 capture. Every one is a strobe glow (on the Lancer's canopy, the Frigate and the station), a glazed window, or the status line, whose surface-voxel count went from 623,094 to 624,538.
- **Scripted:** 5,221 pixels differ, in the same kinds of places, plus the selected Tug's live thumbnail.
- **Flight:** the ship ends at the same position as before, −87.98, −212.91, −234.63, so the materials changed the picture and not the flight.

What the new references hold:

- **WARP:** three default runs and two scripted runs wrote byte-identical PNGs. The statistics are 0.093, 10.6 %, 0.64 % and 0.28 % with 496 colours (default), 0.136, 22.2 %, 2.17 % and 0.68 % with 668 colours (scripted), and 0.135, 21.7 %, 2.12 % and 0.99 % with 726 colours (flight). The flight capture is again a single run.
- **Hardware:** each RTX 3070 Ti Laptop GPU run matches its WARP report under `--expect`: 495, 668 and 729 colours.

## How to check against it

```powershell
.\x64\Debug\Outpost.exe --smoke-test --warp --compare Design\Measurements\SmokeBaselineWarp.png --expect Design\Measurements\SmokeBaselineWarp.txt
```

`--expect` fails the run with exit code 9 when the image statistics leave the stored report's tolerance: 0.005 of mean luminance, 0.3 points of the lit, cyan and warm shares, and 5 % of the colour count (`Neuron::StatisticsTolerance`). `--compare` reports both the composition similarity and an exact count of differing pixels with their bounding box. For a change that leaves the picture alone on WARP, it should read 1.000 and 0 pixels.

## Release builds

The references are Debug captures, and a Release build does not reproduce them pixel for pixel.

- **Measured on 2026-11-22, on the station before PR #3.** A Release WARP capture differed from `SmokeBaselineWarp.png` in 42 pixels, all on edges. It matched `SmokeBaselineWarp.txt` under `--expect`, and two Release runs were identical to each other.
- **It predates this branch.** The same comparison at `381bbc5`, before F4, gave 159 differing pixels. It was made between that commit's own Debug and Release captures, counted with a standard-library PNG decoder, which also reproduced the 42 above.
- **The cause is optimisation-dependent floating-point contraction** under `/arch:AVX2`, which AGENTS.md R16 names.

So check a Release build with `--expect`, and use `--compare` against a capture from the same configuration.

## The scripted reference (plan task F5)

`ScriptedSmoke.txt` drives the executable through input at fixed ticks. Since slice 1 it first presses F9 for the tactical view. It then clicks at the point where the first station had its fighter on the right; in the current station, that point selects the Tug. Then it orbits the view by dragging for 20 ticks and zooms one wheel step. The run lasts until one frame after the last input, 31 frames here.

```powershell
.\x64\Debug\Outpost.exe --smoke-test --warp --script Design\Measurements\ScriptedSmoke.txt --expect Design\Measurements\ScriptedSmokeWarp.txt
```

| File | What it is |
|---|---|
| `ScriptedSmoke.txt` | The script; its format is documented in `GameApp/InputScript.h` |
| `ScriptedSmokeWarp.png` | The WARP capture of its last frame |
| `ScriptedSmokeWarp.txt` | Its WARP report, with the machine-specific lines removed |

**Stored again after slice 1.** The new capture differs from the one before in 517 pixels, all inside two engine plumes: the Frigate's and an escort's, now drawn at an idle ship's length. The statistics are unchanged except the colour count, 647 against 650. Two WARP runs were byte-identical, and the hardware run matched every statistic, colour count included.

What was checked when it was stored again, on the current station:

- The report names the selection: `selected: Tug`.
- The image statistics are mean luminance 0.137, lit 22.2 %, cyan 2.16 % and warm 0.67 %, with 650 distinct colours.
- Two WARP runs wrote byte-identical PNGs.
- The run matches its own report, exit code 0.
- Against the unscripted report it fails with exit code 9, naming the lit and warm shares and the colour count.
- A `--frames` shorter than the script was refused with exit code 1, when it was first stored.
- The RTX 3070 Ti Laptop GPU run matches the WARP report within the tolerance: 649 distinct colours against WARP's 650, the rest identical. So the WARP report also serves as the hardware reference.

## The flight reference (slice 1)

`FlightSmoke.txt` flies the player's escort by its commands, then orders it from the tactical view. Over 480 ticks it:

1. raises the throttle to full;
2. turns hard by the cursor, held at the right edge, away from the Frigate parked ahead;
3. fires the thruster for a second;
4. throttles back to nothing;
5. presses F9 and gives a go-to order by right-clicking a point.

The run lasts 481 frames, so on WARP it takes about ten minutes in Debug.

```powershell
.\x64\Debug\Outpost.exe --smoke-test --warp --script Design\Measurements\FlightSmoke.txt --expect Design\Measurements\FlightSmokeWarp.txt
```

| File | What it is |
|---|---|
| `FlightSmoke.txt` | The script |
| `FlightSmokeWarp.png` | The WARP capture of its last frame, in the tactical view |
| `FlightSmokeWarp.txt` | Its WARP report, with the machine-specific lines removed |

What was checked when it was stored:

- **The prediction matched the server on all 481 ticks**, with no corrections.
- **The ship flies its order.** The report gives the order and the distance left. Shorter runs of the same script showed the distance falling as the ship turned to its point: 707 m at tick 311, 703 m at 400, and 611 m at 480, by then at 80 m/s.
- **No contacts.** Before the script turned away from the Frigate, the same run met the Frigate's collision box three times and was recorded doing so.
- **The flight is the same on every adapter.** The WARP and RTX 3070 Ti Laptop GPU runs ended with the ship at the same position, −87.98, −212.91, −234.63. The hardware frame matches the WARP report under `--expect`: 715 distinct colours against 714, the rest identical.
- **Only one WARP run was made.** It lasts ten minutes, so this capture has not been shown byte-deterministic the way the short ones have; use `--expect` against it rather than `--compare`.
