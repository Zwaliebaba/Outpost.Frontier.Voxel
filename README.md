# Outpost:Frontier — voxel shipyard sample

A runnable Windows sample: a tactical view of an orbital shipyard drawn entirely from MagicaVoxel models with a Direct3D 12 ray-box voxel renderer, under a military command-and-control HUD.

![The sample at 1920×1080, Release build on an RTX 3070 Ti Laptop GPU](Design/Screenshot.png)

What is on screen:
- an asteroid cropped along the left edge;
- the open, warm-lit hangar of a modular shipyard;
- a cyan blueprint of the next hangar section, sitting on the docking socket and built from the same module geometry;
- frigate F-317 with a breached hull section (exposed frames and decks, ember glow, drifting fragments) and twin blue engine plumes;
- two escorts of different classes;
- a tug carrying a prefab module in an orange frame;
- sparse stars and small rocks.

The HUD shows objectives, resources, a radar, the selected ship's panel with its damaged modules, and a build panel with live module thumbnails.

### Expanded station art pass

The editable station kit now has a larger connected assembly: a capital-ship drydock, stacked habitats,
processing and reactor blocks, a command crown and an external radiator array. Thirteen station models
were reauthored and one was added. The solid station is 2.30 times wider and 1.92 times taller than the
original assembly shown above. See [the art review](Design/StationArt.md) for previews, measurements,
authoring tools and validation limits. The previews are offline renders; the Windows GPU sample has
not been recaptured for this art pass.

**The sample is the starting point of a game, not the game.** [Design/GameDesign.md](Design/GameDesign.md) says what the game is: the player flies one ship with a cursor, owns an outpost built module by module on an asteroid, and commands a small fleet from this tactical view. Every solid thing is voxels, and removing voxels is the damage model. The design's build order starts from this scene, and its first six slices make the three objectives on the HUD things a player can complete.

How the code is written is in [AGENTS.md](AGENTS.md); the engineering decisions behind it are in [Design/ADR/](Design/ADR/).

## Requirements

| | Version used to build and verify |
|---|---|
| Visual Studio | 2026 Community 18, workload *Desktop development with C++* |
| Toolset | MSVC `v145` (compiler 14.51), MSBuild 18, `/std:c++latest` |
| Windows SDK | 10.0.26100.0 — supplies D3D12, DXGI, DirectXMath, WIC and `dxc.exe` |
| GPU | Direct3D 12, feature level 12_1, shader model 6.7 — or WARP (`--warp`) |
| CPU | AVX2 (the projects compile `/arch:AVX2`) |

There is no Agility SDK and no `d3dx12.h`. NuGet is the repository's package manager, and libraries are approved one at a time (AGENTS.md R14).

One package is used so far: MsQuic, Microsoft's QUIC implementation, from `Microsoft.Native.Quic.MsQuic.Schannel` 2.6.4 ([ADR-017](Design/ADR/ADR-017-nuget-and-the-msquic-transport.md)).

- Only the `QuicSpike` tool references it.
- The game, its tests and single player never load `msquic.dll`.
- Networked play, the design's slice 10, will use it. That needs Windows 11 or Windows Server 2022, whose TLS 1.3 the Schannel build uses.

## Build and run

Run these from the repository root in a *Developer PowerShell for VS 2026*. Always build through the solution — see AGENTS.md §3 for why.

```powershell
# NuGet packages, once after a clone and whenever a pinned version changes (Visual Studio does this itself)
msbuild Outpost.Frontier.slnx /t:restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo

# Everything, Debug and Release
msbuild Outpost.Frontier.slnx /p:Configuration=Debug   /p:Platform=x64 /m /v:minimal /nologo
msbuild Outpost.Frontier.slnx /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo

# One project: <SolutionFolder>\<Project> for a project in a folder, the bare name at the root
msbuild Outpost.Frontier.slnx "/t:Engine\NeuronClient" /p:Configuration=Debug /p:Platform=x64 /m /nologo
msbuild Outpost.Frontier.slnx /t:Outpost /p:Configuration=Debug /p:Platform=x64 /m /nologo

# Run: borderless fullscreen on the primary monitor
.\x64\Debug\Outpost.exe

# Run in a framed window instead (the size shrinks to fit the work area if it must)
.\x64\Debug\Outpost.exe --windowed --size 1600x900
```

Or open `Outpost.Frontier.slnx` in Visual Studio, set **Outpost** as the startup project and press F5.

Each build copies `Assets\` next to the executable, and the executable finds its assets from its own location, never the working directory. So F5, double-clicking `x64\Release\Outpost.exe`, or running a copied output folder all work. `--assets <dir>` points it at another Assets folder, for example `--assets Assets` to pick up scene edits without rebuilding.

## Controls

A new game starts docked at the Kessler Reach shipyard, with the escort E-114 in its berth. After that, the game resumes where it was last saved (see *Saving*). Every key is a binding in `Assets\Content\Bindings.txt` and can be changed there.

**Docked**

Docked is the tactical view of the station (below), with a section rail at the left (Build, Yard, Fleet, Services) and an inspector at the right. In slice 2 only Services has actions:

- **Launch** (or L) carries the ship out of the hangar and hands it back about 12 seconds later, clear of the station and protected until then. It is unavailable, with the reason, when the ship has no working engine or power plant.
- **Repair** is shown unavailable: the yard does not repair hulls yet.

A control that cannot be used says why. While the station carries the ship, docking or launching, the chase view follows it and the flight HUD names the phase in place of an order.

**Flying**

| Input | Action |
|---|---|
| Hold left button | Steer by the cursor: its offset from the centre is the turn rate. Held under a quarter-second it does nothing, so a click does not jerk the ship |
| Space | Lock cursor steering on or off, without holding the button |
| W / S, mouse wheel | Throttle up and down; the wheel moves it in steps of a fifth |
| X (hold) | Reverse thrust |
| Tab (hold) | Thruster: the boost, drawing its own pool |
| C | Cruise: charges, then carries the ship at 300 m/s. Refused near stations and rocks |
| Z | Kill or restart the engine |
| A / D / R / F (hold) | Strafe left, right, up, down |
| Q / E (hold) | Roll left and right; otherwise the ship banks into its turns by itself |
| Arrow keys (hold) | Pitch and yaw from the keyboard |

The cursor is confined to the window while flying.

**The tactical view**, only while docked (GameDesign.md D10). A click on the docked view's rail or inspector goes to that control; elsewhere:

| Input | Action |
|---|---|
| Left drag | Orbit the camera |
| Left click (no drag) | Select the ship under the cursor; click empty space to clear the selection |
| Right click | An order for your ship. It was flown in slice 1; docked, the station holds the ship and no order is sent, until orders for the fleet arrive |
| Right or middle drag | Pan |
| Mouse wheel | Zoom |
| R or Home | Reset the camera to the scene's starting view |

Ships behind the camera or off screen get an edge arrow that points toward them; a ship behind the camera is labelled *(behind)*. Flying again, with the throttle or the cursor, takes the ship back from an order.

**Always**

| Input | Action |
|---|---|
| G | Dock: asks the nearest station within 10 km. When it answers *proceed*, the station flies the ship to a free berth; *wait* puts it on the waiting list |
| L | Launch, when docked |
| Esc | Cancel: drop an order or the cursor lock, or leave a docking approach or the waiting list |
| H | Show or hide the controls line |
| F3 | Show or hide the status line (adapter, model and voxel counts, fps) |
| F11 or Alt+Enter | Switch between borderless fullscreen and a framed window |
| F12 | Save a screenshot to `Captures\` next to the executable |
| F10 or Alt+F4 | Quit |

## Saving

The game saves itself on launch, on docking and on quit, into one slot, and resumes from it at start-up. There is no manual save and no pause (GameDesign.md D17 to D19). The save is a readable text file at `Saved Games\Outpost Frontier\Save.txt` in your profile ([ADR-011](Design/ADR/ADR-011-character-record-and-save-format.md)); delete it to start a new game. `--save <file>` uses another file.

A save that cannot be read, or that does not fit the content, stops the game with the reason and is left untouched (exit code 11).

## Where things live

| What | Where |
|---|---|
| The design: what the game is, system by system, and the order to build it in | [`Design/GameDesign.md`](Design/GameDesign.md) |
| The MVP implementation plan: the design's first six slices as tasks, tests, ADRs and exit criteria, with what is done so far | [`Design/ImplementationPlan.md`](Design/ImplementationPlan.md) |
| The checkers CI runs, and their self-test | [`Build/`](Build/) — see *Tests and checks* |
| The message catalogue and the in-process transport the executable runs on; the observer that publishes world state | [`GameCore/Messages.h`](GameCore/Messages.h), [`NeuronCore/LoopbackTransport.h`](NeuronCore/LoopbackTransport.h), [`GameLogic/Observer.h`](GameLogic/Observer.h), ADR-008 |
| Zones, the system's stations and their docking points (slice 2, tasks 2.1 and 2.2) | [`Assets/Content/System.txt`](Assets/Content/System.txt), [`GameCore/Zone.h`](GameCore/Zone.h), [`GameCore/DockingPoint.h`](GameCore/DockingPoint.h), [`GameCore/SystemContent.h`](GameCore/SystemContent.h) |
| Docking and launching on the server: space support's checks and waiting list, the approach and the carry in and out (tasks 2.3 and 2.4) | [`GameLogic/SpaceSupport.h`](GameLogic/SpaceSupport.h), [`GameLogic/Universe.h`](GameLogic/Universe.h) |
| The docked view and the UI foundation it is built on (tasks 2.5a and 2.5) | [`GameApp/DockedView.h`](GameApp/DockedView.h), [`NeuronClient/HudBatch.h`](NeuronClient/HudBatch.h), [`NeuronClient/ControlFocus.h`](NeuronClient/ControlFocus.h), [`Assets/Ui/`](Assets/Ui/), ADR-023 |
| The character record and the save (tasks 2.6 and 2.7) | [`GameLogic/SaveGame.h`](GameLogic/SaveGame.h), [`NeuronServer/SaveFile.h`](NeuronServer/SaveFile.h), ADR-011 |
| Ship archetypes, the flight model and the manoeuvre layer (slice 1, tasks 1.1 to 1.3) | [`Assets/Content/Ships.txt`](Assets/Content/Ships.txt), [`GameCore/FlightModel.h`](GameCore/FlightModel.h), [`GameCore/Manoeuvre.h`](GameCore/Manoeuvre.h), ADR-009 |
| The MsQuic spike: the catalogue over QUIC on localhost, with induced loss and a pinned certificate | [`Tools/QuicSpike/`](Tools/QuicSpike/), ADR-017 and ADR-025. Run `.\x64\Debug\QuicSpike.exe`; it exits 0 when every check passes |
| The scene: camera, lights, models, sockets, instances, ships, module damage, engines, objectives, resources, build items | [`Assets/Scene.txt`](Assets/Scene.txt) — plain text, documented in its header and in ADR-002 |
| The models | `Assets/Models/*.vox` — genuine MagicaVoxel files, open them in MagicaVoxel |
| The canonical palette (role → colour) | [`GameCore/Palette.h`](GameCore/Palette.h) for the index map, [`GameCore/Palette.cpp`](GameCore/Palette.cpp) for colours and the emissive material table |
| VOX → world conversion | [`NeuronCore/ModelAxes.h`](NeuronCore/ModelAxes.h), explained in ADR-002 |
| Renderer and shaders | [`NeuronClient/Renderer.cpp`](NeuronClient/Renderer.cpp), [`NeuronClient/Shaders/`](NeuronClient/Shaders/), explained in ADR-003 |

**VOX to world.** VOX is right-handed, +Z up, with ships facing +Y; the world is Direct3D left-handed with +Y up. A VOX point `v` in a model with pivot `p` and voxel size `s` lands at `((v.x−p.x), (v.z−p.z), (v.y−p.y))·s`, then goes through the instance's scale, yaw/pitch/roll and position. Socket, engine and light positions in `Scene.txt` are VOX coordinates of their model.

### Editing the models

Damage-ready vessel masters are available as `Assets/Models/*Intact.vox`, with region annotations
and a reproducible F-317 initial-damage fixture in `Assets/Hulls/`. See
[the hull asset guide](Design/DamageReadyHulls.md) for previews and validation. The existing scene
keeps its original model files; live damage and repair integration remain the MVP's slice 3.

Edit the committed files under `Assets/Models/` directly in MagicaVoxel. Keep one model per file and preserve its palette indices. Rebuild Debug or Release to copy saved edits beside the executable, or launch with `--assets Assets` from the repository root. If geometry moves relative to its origin, check its sockets, lights and engines in `Assets/Scene.txt`.

A **new** asset file only has to be put under `Assets\`: a post-build step copies the whole folder next to the executable on every build ([ADR-024](Design/ADR/ADR-024-assets-deployed-by-a-post-build-copy.md)). Visual Studio does not notice an asset-only edit, so after changing only assets, build from the command line or *Rebuild* `Outpost`.

Every file carries the identical canonical RGBA palette chunk. Index 0 is empty, and voxel colour `c` is chunk entry `c−1`.
The [signal and glazing guide](Design/SignalMaterials.md) describes bright yellow strobes (224)
and see-through blue glass (225). VOX files include matching MagicaVoxel material attributes;
the game supplies the canonical emission, double-flash timing and glass coverage.

Import [`Assets/Palettes/OutpostFrontier.png`](Assets/Palettes/OutpostFrontier.png) into MagicaVoxel
to reuse the same colors and alpha. Regenerate the strip with `python Tools/ExportPalette.py`;
Emit/Glass properties stay in the VOX files.

### Gameplay UI proposal

The [gameplay UI proposal](Design/GameplayUiProposal.md) includes Flight, Combat, Mining and
Docked layouts, screenshots, interaction checks and the requested DDS texture plan. Download/open
[the interactive preview](Design/GameplayUi/gameplay-ui-preview.html) in a browser to review it.
These are design review files with example data. The UI foundation and the Docked layout's rail and
Services inspector are built (tasks 2.5a and 2.5); the other layouts are not yet.

## Tests and checks

```powershell
# Unit tests: every suite the build produced (no GPU needed)
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\Extensions\TestPlatform\vstest.console.exe" (Get-ChildItem x64\Debug -Filter *Tests.dll).FullName /Platform:x64

# Smoke test: fixed timeline on WARP, image checks, debug layer on; exit code 0 on success
.\x64\Debug\Outpost.exe --smoke-test --warp --capture smoke.png --report smoke.txt --compare "Design\Space Station HUD Without Navigation Path.png"

# The checkers CI runs (AGENTS.md §6). The pinned tools: pip install clang-format==18.1.3 clang-tidy==22.1.8
python Build\CheckProjectFiles.py    # build shape, registration, Debug/Release alignment, R2, R7, R11, R14
python Build\CheckFormat.py          # clang-format over every .h/.cpp; --fix rewrites
python Build\RunClangTidy.py         # after a build, in a Developer PowerShell; --dry-run prints the commands
python Build\TestCheckers.py         # the checkers' own tests
```

Every library has its own test project under `Tests\`. In Visual Studio, *Test › Test Explorer* runs them all.

| Project | Tests | Covers |
|---|---|---|
| `Tests\NeuronCoreTests` | 45 | VOX read/write and a hand-authored fixture that pins the palette offset; rejection of bad magic, truncated chunks, invalid dimensions and out-of-bounds voxels; meshing of single and adjacent voxels; wireframes and the side profile; VOX-to-world axes, handedness and socket mating; the wire format's byte order and round trips; the in-process transport's order and datagram budget. The DDS reader: each supported format, truncated, unsupported and mipmapped files refused, straight alpha required (ADR-023). Text records: round trips and what the format cannot hold |
| `Tests\NeuronClientTests` | 46 | Selection maths: ships ahead, beside the view, straddling the near plane and behind the camera; pick rays; click versus drag; orbit-camera limits; pixel differences between captures, the report's image line, and the `--expect` tolerance. Bindings: key names, contexts and their precedence, holds and repeats, a key held across a context switch. The chase camera: its offset at rest, lag, look-ahead, levelling and shake. The UI foundation: sprites and nine-slice panels in the HUD batch, the sprite manifest and its errors by line, button skins, and control focus with pointer, keyboard and disabled controls |
| `Tests\NeuronServerTests` | 12 | The tick clock at 60 Hz (ADR-007): whole ticks, carried remainders, ten minutes exactly 36,000 ticks, stall catch-up, interpolation. The save file: sections round trip as readable records, a file cut short or miscounted is refused, and writing replaces the file whole |
| `Tests\GameCoreTests` | 93 | The canonical palette, including byte equality across every model; scene parsing and validation; the shipped blueprint landing on its dock; the message catalogue: round trips, header, refusals, routes, the datagram budget, and applying messages to the client's world; world objects and poses; ship records; the flight model against chapter 02's worked example; the manoeuvre layer; swept collisions at every speed, bounce and collision damage. Zones: chapter 01's immersion, edges, boxes and cylinders, the smallest zone winning, hazards. Docking points, stations and the system file, with mistakes named by file and line |
| `Tests\GameLogicTests` | 44 | The universe: its objects, the first state, exact income per minute, fractional income, capacity, reloading; the observer: one message per object, nothing sent twice, only what changed. Flight: an input timeline flown the same at 30, 60 and 144 fps, a go-to directive, a missing command, walls met at every speed regime, cruise refused near a wall, contacts recorded once, commands across the loopback. Space support: the checks in the chapter's order, the nearest free point, the waiting list and the 90 s abandonment. Docking from four start poses, launch clear and protected, commands set aside while carried, the Dock key out of range, cancel, a refused launch. The save: every field round trips, an unknown section is kept, a launch-fly-dock-quit-restart loop and a save that does not fit the content |
| `Tests\GameAppTests` | 37 | The HUD: every vertex on screen at 720p, 1080p and 4K with ships off screen and behind the camera; build slots; scaling; fonts. The flight HUD on screen at the same sizes. The draw constants the voxel shader unprojects through, and the render origin. Input scripts: parsing, drags, clicks and keys expanded per tick, mistakes reported with the line. Mouse flight: demand from the cursor, the hold, the lock, throttle, cruise, banking. The prediction against the server's step, corrections, and interpolation. The docked view: rail and inspector on screen and clear of the HUD at three resolutions, Launch and Repair with their reasons, clicks, and the rail's sections |

Smoke-test options:

- `--frames N`, `--size WxH`, `--no-vsync`, `--debug-layer` / `--no-debug-layer` and `--assets <dir>`.
- `--script <file>` plays input commands at fixed server ticks: `drag`, `click`, `move`, `wheel`, `key` (held for a number of ticks), `press`, `reset`, `help` and `status`. Keys are named as in `Bindings.txt`. The examples are `Design\Measurements\ScriptedSmoke.txt` (the tactical view) and `FlightSmoke.txt` (flying, then an order). `FlightSmoke.txt` predates the docked start and is due to be rewritten; see SmokeBaseline.md.
- `--expect <report>` fails the run when the image statistics leave a stored report's tolerance.

`--benchmark` times every frame on the GPU. The F6 crowd scenes it was measured on came from a generator that left the tree with `AssetGenerator` ([F6-RendererBaseline.md](Design/Measurements/F6-RendererBaseline.md)).

Checking against the stored references is described in [SmokeBaseline.md](Design/Measurements/SmokeBaseline.md). The smoke test always renders in a window of exactly `--size`, never fullscreen, so its captures do not depend on the monitor.

Exit codes:

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Bad command line |
| 2 | No suitable device (FL 12_1 / SM 6.7) |
| 3 | Scene invalid |
| 4 | Render failure |
| 5 | Debug-layer errors |
| 6 | Image check failed (blank, flat, or missing the cyan HUD or warm lights) |
| 7 | Capture failed |
| 8 | The client could not decode or apply the server's messages |
| 9 | `--expect`: the statistics differ from the stored report |
| 10 | The client's prediction of its own ship left the server's on a tick the server did not correct |
| 11 | The save could not be read or does not fit the content; it is left untouched |

### What was run for this version

On 2026-11-22, on branch `slice-2-dock` (slice 2, tasks 2.1 to 2.7 and 2.5a; 2.8 is not done), Windows 11 with an RTX 3070 Ti Laptop GPU:

- **Builds:** Debug and Release x64 through the solution, with no warnings. The post-build copy deployed `Assets\` in both (ADR-024).
- **Unit tests:** `vstest.console.exe` over all six suites, Debug: 277 of 277 passed.
- **Smoke tests on WARP, Debug:**
  - The default and scripted runs were stored again as references, for the docked start (SmokeBaseline.md). Two runs of each wrote byte-identical PNGs.
  - The flight reference was not run: its script predates the docked start.
  - The CI material sample, 3 frames at 1280×720, passes.
- **Smoke tests on hardware, Debug:**
  - The default and scripted runs match their WARP references under `--expect`, with 0 debug-layer errors.
  - A scripted round trip: Launch clicked in the Services panel, the carry-out, free flight, G, the approach, and docked again with the docked view. The prediction matched the server on every tick it predicted, with one tick corrected where the server took the ship.
- **The save:** two runs with `--save`, a new game and then a resume; the file is a readable text record.
- **Checkers:**
  - `CheckProjectFiles.py` clean.
  - `CheckFormat.py` clean with clang-format 18.1.3.
  - `RunClangTidy.py` clean over 107 translation units, with the local clang-tidy and `--any-version`.
  - `TestCheckers.py` passed after the Universe rename.

Not run here: the GitHub Actions workflow before the pull request, Visual Studio's own build of an asset-only change (ADR-024), and an interactive session played by hand.

The record before, for slice 1 on branch `slice-1-flight`. On 2026-11-22 (slice 1, tasks 1.4 to 1.11), Windows 11 with an RTX 3070 Ti Laptop GPU:

- **Builds:** Debug and Release x64, each a clean `Rebuild` through the solution, with no warnings.
- **Unit tests:** `vstest.console.exe` over all six suites, Debug: 191 of 191 passed.
- **Smoke tests on WARP, Debug:**
  - The default run, the scripted run and the flight run each match their stored references (SmokeBaseline.md).
  - The client's prediction matched the server on every tick of every run.
  - Release matches the default reference to the pixel and the scripted one under `--expect`.
- **Smoke tests on hardware:** the same three runs match their references under `--expect`, with 0 debug-layer errors.
- **Benchmark:** Release, three runs in each of three views (ADR-010). The worst p50 is 4.1 ms per frame, with the shadow map redrawn every frame.
- **Checkers:**
  - `CheckProjectFiles.py` clean.
  - `CheckFormat.py` clean with clang-format 18.1.3.
  - `RunClangTidy.py` clean over 81 translation units, with the local clang-tidy 22.1.3 and `--any-version`.
  - `TestCheckers.py`: 37 passed and 4 skipped.

- **By hand:** the owner flew the interactive build.

Not run here: the GitHub Actions workflow.

The record before that, for F0 to F7 and slice 1 tasks 1.1 to 1.3, on branch `mvp-foundations`:

- **Builds:** a Debug x64 rebuild through the solution after a NuGet restore, with no warnings. Release x64 built too.
- **Unit tests:** `vstest.console.exe` over all six suites, Debug: 135 of 135 passed.
- **Smoke test on WARP:**
  - Debug: 0 pixels differ from `Design\Measurements\SmokeBaselineWarp.png`, and the statistics match its report.
  - The scripted run matches its own stored reference.
  - Release matches the statistics but not every pixel; see SmokeBaseline.md.
- **Smoke test on hardware:** Debug with the debug layer on: exit 0, 0 debug-layer errors, and statistics matching `SmokeBaselineGpu.txt`.
- **QuicSpike:** every check passed, Debug at 600 ticks and Release at 300 (ADR-017).
- **Checkers:**
  - `CheckProjectFiles.py` clean.
  - `TestCheckers.py` 41 tests passed.
  - `CheckFormat.py` clean with clang-format 18.1.3, the pinned version.
  - `RunClangTidy.py` clean over 73 translation units. That was the local clang-tidy 22.1.3 with `--any-version`; CI pins 22.1.8.

Not run here: the GitHub Actions workflow itself, and an interactive session driven by hand.

The earlier record, for the sample as first published: all of the following passed on 2026-11-21, on Windows 11 with an RTX 3070 Ti Laptop GPU:
- **Builds:** Debug and Release x64 through the solution, with `/warnaserror`: zero warnings.
- **Unit tests:** `vstest.console.exe` over all six `*Tests.dll` suites, Debug and Release: 55 of 55 passed.
- **Smoke test on WARP**, Debug (debug layer on) and Release, at 1920×1080:
  - exit 0; 0 debug-layer errors and 0 warnings;
  - mean luminance 0.146, lit 26.8%, cyan 1.92%, warm 2.63%, 658 colours;
  - composition similarity 0.794 to the reference image.
- **Smoke test on hardware:** Release, 120 frames at 1920×1080, and Debug with the debug layer, 60 frames at 1280×720. Both exited 0 with 0 debug-layer errors. The screenshot above is the Release capture.
- **Interactive Debug build, driven by a script:** two resizes, minimise to 0×0, restore, another resize, an orbit drag, an F12 capture, then close. It exited 0 with 0 debug-layer errors. The close path was repeated 10 times after the shutdown fix described in ADR-003, with no hangs.
- **Formatting:** clang-format **18.1.3**, the version CI pins, over every `.h`/`.cpp`: no changes needed.
- **clang-tidy:** the repository's `.clang-tidy` over all 42 translation units: 0 findings. This was the 22.1.3 shipped with Visual Studio; CI pins 22.1.8.

Not run here:
- the GitHub Actions workflow itself;
- `Build/CheckFormat.py`, `Build/CheckProjectFiles.py` and `Build/RunClangTidy.py`, which did not exist yet. They were added on 2026-11-22 and CI runs them; see *Tests and checks*.

## Scope

In scope: one static tactical scene; camera control; ship selection and inspection; HUD; editable voxel models; tests.

Out of scope, by the brief: a ship editor, live destruction, combat and networking. The client builds its picture of the world from the server's encoded messages over the in-process transport. `QuicSpike` carries the same messages over MsQuic, so that is where a network transport goes.

Where the design puts those ([Design/GameDesign.md](Design/GameDesign.md) §11): combat and live destruction arrive together in slice 3, as voxel damage; networking is slice 10, over QUIC with MsQuic (GameDesign.md §8.1), and the rules that keep it possible bind from slice 1 (AGENTS.md R18 to R25); a ship editor is not part of the game. The hull and module percentages and the resources the HUD shows are read from `Scene.txt` today; in the design they are measured from voxel masks and produced by the outpost.
