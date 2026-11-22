# Smoke-test baseline

**Taken:** 2026-11-22, first at commit `381bbc5` (F1 to F3 done), and stored again after F4, after the new station, after slice 1, after the signal and glazing materials and for slice 2's docked start, for the reasons given below. The hazard reference and the rewritten flight script were added for slice 2's task 2.8. **Purpose:** the reference that every change that must not alter the picture is checked against.

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

## Why it was stored again for slice 2's docked start

Since slice 2's docking work, a new game starts docked at the shipyard (task 2.7), and docked is the tactical view with the docked view's section rail and Services inspector over it (task 2.5). The default run then showed the tactical view where its references held the flight view, and it failed `--expect` on every statistic: lit 25.0 % against 10.6 %. The references were stored again on branch `slice-2-dock`, at the docked view's commit `f413833`.

- **Default:** the report reads `view: tactical` and `save: a new game, docked at Shipyard, saved nowhere`. The first publish is 70 messages, where it was 69: the dock status is new. No tick is predicted, because the ship is in its berth and the server holds it. The statistics are mean luminance 0.139, lit 25.0 %, cyan 2.57 % and warm 1.03 %, with 786 distinct colours.
- **Scripted:** `ScriptedSmoke.txt` no longer presses F9, which task 2.5 removed; its click still selects the Tug. The statistics are 0.141, 25.8 %, 2.58 % and 0.73 %, with 671 colours. Against the default report it fails with exit code 9, as it should.
- **WARP:** two runs of each wrote byte-identical PNGs.
- **Hardware:** the RTX 3070 Ti Laptop GPU matches each WARP report under `--expect`: 788 colours against 786 (default) and 673 against 671 (scripted).
- **The flight reference was not stored again.** `FlightSmoke.txt` assumed the game starts in flight and pressed F9. The script has since been rewritten to launch first (see *The flight reference* below); its capture is still due, and until then `FlightSmokeWarp.*` are the slice 1 capture and do not describe the game.

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

`ScriptedSmoke.txt` drives the executable through input at fixed ticks. A new game starts in the tactical view since slice 2, so it no longer presses F9 first. It clicks at the point where the first station had its fighter on the right; in the current station, that point selects the Tug. Then it orbits the view by dragging for 20 ticks and zooms one wheel step. The run lasts until one frame after the last input, 31 frames here.

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

## The hazard reference (task 2.8)

`HazardSmokeSave.txt` is a save with the player's ship at rest in space under the Frigate, inside the `FrigateBreach` zone: the pose `FlightSmoke.txt` ends in. Resuming it shows the flight HUD with the hazard warning after three frames, so the warning can be drawn and checked without the flight that leads to it. The game saves when it quits, so resume a copy:

```powershell
Copy-Item Design\Measurements\HazardSmokeSave.txt Save.txt
.\x64\Debug\Outpost.exe --smoke-test --warp --save Save.txt --capture HazardWarning.png --report HazardWarning.txt
```

CI's Windows job does this on every run, at 1280 x 720, fails unless the report names the warning, and uploads the capture and the report as the `hazard-review` artifact.

**Where the save came from.** The game's own save code wrote it (`Universe::MakeSave` and `FormatSaveGame`), in the headless replica described under the flight reference, with the flight script's inputs and 390 more ticks at rest. It was not written on Windows.

**What CI's run showed**, on 2026-11-22 at commit `76154d8`, Debug on WARP, run 29 of the Build workflow:

- `save: resumed HazardReview\Save.txt at tick 1501, in space`, and `view: flight`.
- `own ship: at 55.65, -98.50, 3.14, 0.00 m/s, 0 contact(s)`.
- `hazard warning: FrigateBreach, immersion 0.30, scanner -18 %, danger to hull`.
- The prediction matched the server on all 3 ticks.
- `image 1280x720: mean luminance 0.084, lit 6.4%, cyan 0.94%, warm 0.26%, 407 distinct colours`, and `debug layer: on, 0 error(s), 0 warning(s)`.

**Nobody has looked at the frame.** The report proves the warning was computed and that the HUD drew without a debug-layer error. Whether it reads well is for a person to judge from the artifact, or from a run by hand. No capture is stored here: one at 1920 x 1080 from a Windows machine can be, as `HazardSmokeWarp.png` and `.txt`.

`GameLogicTests` keeps the save fitting the content: it restores in space, inside the zone, and a second of ticks neither moves the ship nor touches anything.

| File | What it is |
|---|---|
| `HazardSmokeSave.txt` | The save, a text record (ADR-011) |

## The flight reference (slice 1, rewritten for slice 2)

**The script is new and its capture is not stored yet.** `FlightSmoke.txt` was rewritten on 2026-11-22 for the docked start and for task 2.8's hazard warning. `FlightSmokeWarp.png` and `FlightSmokeWarp.txt` are still the slice 1 capture: they show a run the script no longer makes, and they stay only until one WARP run of the new script replaces them. That run needs Windows; it is the open item of slice 2's close-out.

Over 1,110 ticks the script:

1. clicks Launch in the Services inspector at tick 5, and the station carries the ship out of the hangar;
2. from tick 720, raises the throttle to four fifths and turns hard right by the cursor, held at the right edge, which clears the Frigate's stern;
3. holds the cursor below the centre, so the ship flies half a loop downwards and comes back level under the Frigate;
4. throttles back to nothing and stops below the breach, inside the `FrigateBreach` zone, with the hazard warning on the HUD.

The run lasts 1,111 frames. The slice 1 run took about ten minutes for 481 frames on WARP in Debug, so expect about 25 minutes.

```powershell
.\x64\Debug\Outpost.exe --smoke-test --warp --script Design\Measurements\FlightSmoke.txt --capture Design\Measurements\FlightSmokeWarp.png --report Design\Measurements\FlightSmokeWarp.txt
```

Remove the machine-specific lines (`assets:`, `captured:`) from the report before storing it, as for the others.

**What the run should show.** The script was not run on Windows. It was written against a headless replica of the smoke loop, built on Linux with g++ from this tree's own sources: the server's `Universe` and `Observer`, the loopback transport, and the client's `CommandMap`, `FlightInput`, `Prediction`, `ChaseCamera`, `ControlFocus` and `DockedView`, wired as `TacticalClient` and `Main.cpp` wire them, without the renderer. In that replica:

- The click at tick 5 is taken by Launch, and the ship is released at tick 700 at 0, 16, -44, flying at 30 m/s.
- **The prediction matched the server on all 411 ticks it predicted**, with no corrections and no contacts.
- **The ship ends at rest at 55.65, -98.50, 3.14**, which is 50.9 m from the zone's centre. The report line should read `hazard warning: FrigateBreach, immersion 0.30, scanner -18 %, danger to hull`.
- The ship's collision sphere, 30.9 m in radius, never comes closer than 6.6 m to a body. At the end it is 6.6 m clear of the Frigate's box above it and 9.1 m inside the zone's surface.
- **The run does not depend on the exact tick of the release.** With every flight input moved 19 ticks earlier, and 120 ticks later, the ship ends at the same position to the centimetre. So a release anywhere from 120 ticks before tick 700 to 19 after it flies the same path.

MSVC's floating-point results differ from g++'s in the last place (AGENTS.md R16), so the run on Windows may not end at exactly those figures. If it ends outside the zone or records a contact, the script needs adjusting: that is a finding about the script, not about the game.

**Its last frame has been drawn on Windows.** The hazard reference below resumes a save of the pose the script ends in, and CI drew it on WARP. There the ship sits at 55.65, -98.50, 3.14 and the report reads the warning line above, word for word, so MSVC and the replica agree on where the run ends. In the replica, the ship's orientation and the chase camera's forward and up at the script's last frame equal the saved pose's to four decimals; the camera's offset differs by 0.2 m. So that frame is a rehearsal of this run's capture, at 1280 x 720. Two things about it are worth knowing before spending 25 minutes on the run:

- **The lit share is 6.4 %, against the image check's floor of 5 %.** The view from under the Frigate is mostly open space. The run should pass the check, with 1.4 points to spare.
- **The ship ends upside down.** Half a loop downwards leaves it inverted, and with no turn demanded nothing rolls it upright.

**Why under the Frigate.** The breach vents a trail of debris, each piece a collision body, on the side of the hull the zone is centred on, and the Lancer's sphere cannot pass between them. Above the hull, the part of the zone the sphere can reach without touching the Frigate's box is a band about 4 m deep. Below the hull that band is about 16 m deep, so that is where the run ends.

`GameAppTests` checks that both stored scripts parse, and that the flight script's first click lands on the docked view's Launch button at 1920 x 1080.

| File | What it is |
|---|---|
| `FlightSmoke.txt` | The script |
| `FlightSmokeWarp.png` | *Out of date:* the slice 1 capture, in the tactical view |
| `FlightSmokeWarp.txt` | *Out of date:* the slice 1 report |

### The slice 1 run, for the record

Until slice 2, the script flew from a start in flight, pressed F9 for the tactical view and gave a go-to order. What was checked when that capture was stored:

- **The prediction matched the server on all 481 ticks**, with no corrections.
- **The ship flew its order.** Shorter runs of the same script showed the distance falling as the ship turned to its point: 707 m at tick 311, 703 m at 400, and 611 m at 480, by then at 80 m/s.
- **No contacts.** Before the script turned away from the Frigate, the same run met the Frigate's collision box three times and was recorded doing so.
- **The flight was the same on every adapter.** The WARP and RTX 3070 Ti Laptop GPU runs ended with the ship at the same position, −87.98, −212.91, −234.63. The hardware frame matched the WARP report under `--expect`: 715 distinct colours against 714, the rest identical.
- **Only one WARP run was made**, so that capture was never shown byte-deterministic the way the short ones have.
