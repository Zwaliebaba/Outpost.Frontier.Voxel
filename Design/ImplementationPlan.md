# Outpost:Frontier — MVP implementation plan

**Status:** in progress; the foundations and slice 1 are done, and slice 2 is built, with two checks on Windows hardware left to its close-out · **Date:** 2026-11-22

This plan turns the first six slices of [GameDesign.md](GameDesign.md) §11 into work: what to build, in which project, behind which decision, with which tests, and how to tell a slice is finished. It assumes the code as it stands at commit `d331bcf` and the rules in [AGENTS.md](../AGENTS.md), which it does not repeat. It gives no time estimates; the order and the dependencies are the plan.

## Progress

| Task | State | Where |
|---|---|---|
| F1 Checkers | Done | `Build/`: `CheckProjectFiles.py`, `CheckFormat.py`, `RunClangTidy.py`, and `TestCheckers.py` (41 tests). CI runs all four; the first real clang-tidy run was clean over 47 translation units |
| F2 Tick | Done | 60 Hz, counted exactly ([ADR-007](ADR/ADR-007-simulation-tick-time-and-position.md)). Double-precision positions, also decided there, arrive with F4 |
| F3 Messages | Done | `NeuronCore/WireFormat`, `NeuronCore/LoopbackTransport`, `GameCore/Messages` ([ADR-008](ADR/ADR-008-messages-encoding-and-the-in-process-transport.md)). Since F4 the executable runs on them |
| F0 Smoke baseline | Done | [SmokeBaseline.md](Measurements/SmokeBaseline.md). WARP is byte-deterministic, so its capture is an exact reference. It was stored again after F4, whose pose round trip moves edge pixels |
| F4 Server-owned world | Done | `GameLogic/Universe` and `GameLogic/Observer`, `GameCore/UniverseObject`, `ObjectSpawn` and `ObjectState` in catalogue version 2 ([ADR-009](ADR/ADR-009-content-records-and-identifiers.md)). The client builds its `UniverseSnapshot` from decoded messages |
| F5 Scripted smoke | Done | `--script` and `--expect`, `GameApp/InputScript`, and a stored scripted reference. The optional CI WARP step is not added |
| F6 Renderer baseline | Done: go | [F6-RendererBaseline.md](Measurements/F6-RendererBaseline.md). ADR-003's technique stays |
| F7 MsQuic spike | Done | `QuicSpike` ([ADR-017](ADR/ADR-017-nuget-and-the-msquic-transport.md)). Every check passes at 10 % loss each way. At 30 % the stream stays whole, but the worst pose is 49 ticks behind |
| Slice 1 | Done | The escort E-114 flies by cursor with a chase camera, and obeys a go-to order from the tactical view (F9). The prediction matched the server on every tick of every scripted run ([ADR-010](ADR/ADR-010-moving-instances-and-the-shadow-map.md)). The owner flew it by hand on 2026-11-22 |
| Slice 2 | Built: 2.1 to 2.8 and 2.5a. Open: two checks on hardware | Zones and docking points in `Assets/Content/System.txt` (`GameCore/Zone`, `DockingPoint`, `SystemContent`). The dock request is `GameLogic/SpaceSupport`, not `Docking`, and the carry in and out is a mover inside `GameLogic/Universe`, not `GameCore/Manoeuvre`. The docked view is `GameApp/DockedView`, not `BaseView`: R2 bans the affix. The save ([ADR-011](ADR/ADR-011-character-record-and-save-format.md)) resumes the game, and a launch-fly-dock-quit-restart loop passes as a scenario test and as a scripted run on hardware. The hazard warning (2.8) is `GameApp/HazardWarning`, drawn by the flight HUD; scanner interference is its read-out until slice 3 brings a scanner (GameDesign.md D21). The flight smoke script is rewritten for the docked start: it launches, flies under the frigate and ends in the breach with the warning showing. It was written and checked in a headless replica of the client's input path, not run. CI draws the warning on WARP from a stored save of the pose that script ends in, and its report agrees with the replica. **Open, both on Windows hardware:** one WARP run of `FlightSmoke.txt` to store `FlightSmokeWarp.*` again ([SmokeBaseline.md](Measurements/SmokeBaseline.md)), and the loop played by hand. Assets deploy by one post-build copy ([ADR-024](ADR/ADR-024-assets-deployed-by-a-post-build-copy.md)) |
| Slices 3 to 6 | Not started | — |

## 1. What the MVP is

**The MVP is slices 1 to 6 of the design: one playable scene.** The sample shipyard, in its one system, becomes a game. The player flies an escort by cursor, docks and launches, fights in voxels, builds the hangar extension, mines the asteroid and hauls ore, commands a wing, and survives a raid. The three objectives on the HUD today become things that can be completed, failed and saved:

| Objective | Completed by | Slice that makes it real |
|---|---|---|
| Defend the shipyard | Destroying or driving off a raid, from the cockpit or, docked at the outpost, from the tactical view | 6 (raid), on 3 (combat) |
| Finish hangar extension F-03 | Building the planned `HangarSection` on the `ExtensionDock` socket | 4 |
| Escort tug T-041 to dock | The tug reaching its docking point alive through an ambush | 6, on 2 (docking) and 5 (tug itinerary) |

**Not in the MVP:** factions and regard, ambient population beyond raids, markets, worth and level, a second system, lanes, jumps, routes, the map, the job board, the trigger language and the campaign, multiplayer transport. Section 11 lists what the MVP stubs in their place.

**The MVP is single-player, but built for 100 players.** GameDesign.md §8.2 sets the scale target of 100 players in one scene. The MVP builds none of the multiplayer features, but nothing in it may need rewriting to reach that scale: section 14 lists what the MVP does now for that reason, and what comes after it.

**Decisions this plan takes as settled**, recorded in GameDesign.md §13 by the same commit:

| # | Decision | What it does to the MVP |
|---|---|---|
| D1 | Death returns the player to the last base with the ship as fitted there; cargo is lost | Slice 3 builds the base snapshot and the return. In the MVP the last base is the outpost |
| D2 | The command block cannot be destroyed | Slice 3 gives the authority an indestructible flag; slice 6 raids can strip everything else |
| D5 | The DESIGN tab becomes YARD | Renamed in slice 4, filled in slice 6 |
| D6 | Only fighters and freighters are flyable | The two escort hulls are the flyable ships of the MVP; frigate, tug and miners are commanded |
| D12 | A wing of about three, capped by crew | Slice 6 |
| D3 | One claim | The MVP's one outpost is the design's whole first release, not a cut |
| D4 | Raids are sized by place and standings | With no factions in the MVP, by the claim's zone alone (task 6.5) |
| D7 | The outpost does not sell to traffic | No market module in the MVP or later |
| D10 | The tactical view only while docked | It is the docked view of slice 2. Slice 1 has no in-flight tactical view; a debug key stands in until docking exists. In slice 6, tactical orders are given from a berth |
| D11 | Text comms, no voice | No audio content beyond effects in the MVP |

D8 (setting before slice 7) and D9 (a short campaign) fall outside the MVP. D13 (about twenty claim sites per system) came out of the scale review and bounds F6's crowd scene. All thirteen are recorded in GameDesign.md §13.

## 2. Where the code starts

The sample is a sound base, but it was built as a still picture. These are the facts about it that shape the plan.

| Fact | Where | Consequence |
|---|---|---|
| The server does not know where anything is. `World` holds ship health, objectives and resources; positions live only in the client's `SceneDefinition` | `GameLogic/World.*`, `GameCore/WorldSnapshot.h` | Before anything moves, the server must own world objects with identities and poses (F4) |
| The server is loaded from the scene the client parsed: `world.Load(session->client.Scene())` | `Outpost/Main.cpp` | Allowed by R18 (a `GameCore` type handed over by the executable), but the server should load its own content (F4) |
| The snapshot is passed by reference, once per frame | `Outpost/Main.cpp` | A message flow replaces it (F3 built it; F4 uses it) |
| The tick is 20 Hz | `NeuronServer/TickClock.h` | Too coarse for cursor flight without prediction; ADR-007 sets the rate (done: 60 Hz) |
| Every instance of a model shares one `LoadedModel` and one GPU buffer | `GameApp/SceneAssets.*` | Damage needs a surface per damaged instance (slice 3) |
| The shadow map is drawn once; the shadow camera is fitted to the static scene | `GameApp/TacticalClient.cpp`, `TacticalScene::BuildShadowCamera` | Moving ships need it redrawn (slice 1) |
| Instance transforms are computed once in `TacticalScene::Build` | `GameApp/TacticalScene.cpp` | Poses must come from the snapshot every frame (slice 1) |
| Input is mouse orbit, pan, zoom and select, and six keys | `Outpost/MainWindow.cpp`, `GameApp/TacticalClient.*` | A command layer is needed before flight (slice 1) |
| HUD content (objectives, resources, build items) comes from `Scene.txt`; resources grow by an authored `perminute` | `Assets/Scene.txt`, `GameLogic/World.cpp` | Replaced by outpost state in slice 4 |
| The three checkers AGENTS.md relies on did not exist, and CI skipped their gates | `Build/`, `.github/workflows/build.yml` | Written first (done: F1) |
| No uncapped frame time has ever been measured (ADR-003) | — | A baseline is measured before the renderer changes (F6) |
| The smoke test's only reference was the concept image, at 0.794 composition similarity: it could not see a pose regression | `Design/Space Station HUD Without Navigation Path.png` | A capture of today's frame is stored before F4 changes anything (F0) |

## 3. How every slice is verified

The same four kinds of evidence, every slice. A slice is not finished until all four exist for what it adds.

1. **Unit tests** in the suite of the library that owns the code, through its public headers, without a GPU (ADR-006).
2. **Scenario tests** in `GameLogicTests`: a `Universe` built from shipped or fixture content, driven by a list of commands for a fixed number of ticks, with the outcome asserted. Each slice's exit criterion is written as one of these where it can be, so that "it works when" is a test and not an opinion.
3. **The smoke test**, extended with `--script <file>`: a timeline of input commands played at fixed ticks, so the executable can be driven through flight, docking and a fight on WARP and its capture checked (F5).
4. **A run by hand** on hardware, as AGENTS.md requires for anything that touches presentation, with frame time read from the status line.

**Performance budget, MVP-wide:** a frame at 1920×1080, Release, on the reference GPU (the RTX 3070 Ti Laptop the README names), stays under 16.7 ms with v-sync off, including the frame in which voxels are carved. A carve is visible in the frame after the tick that issued it. WARP is a correctness gate, never a performance one.

**Determinism check, from slice 3:** every scenario test that carves voxels also runs the client's application of the same damage events and asserts that the two masks hash equal (R22).

## 4. Foundations (before slice 1)

Seven pieces of work that every slice stands on. None of them is visible to a player.

**ADRs due:** ADR-007, ADR-008, ADR-009, and ADR-017 before F7 (section 12).

| # | Task | Where | Tests |
|---|---|---|---|
| F1 | Write the three checkers AGENTS.md names: `Build/CheckFormat.py` (clang-format over the tree, `--fix`), `Build/CheckProjectFiles.py` (build shape, project registration, R2 affixes, R7 file names, R11 spellings, flat directories, functional filters, Debug/Release alignment), `Build/RunClangTidy.py`. CI picks them up through its existing guards | `Build/` | Each checker passes over today's tree and fails on a planted violation of every rule it claims |
| F2 | **Done** ([ADR-007](ADR/ADR-007-simulation-tick-time-and-position.md)). The simulation ticks at 60 Hz on both halves, and `TickClock` counts wall time exactly: a tick of 16,666.67 microseconds is not rounded to a whole number. Ten minutes are 36,000 ticks; whole-microsecond ticks would have given 36,001. State is sent to clients at a lower rate than the tick (GameDesign.md §8.2), so the rate does not set bandwidth. ADR-007 also decides double-precision world positions and camera-relative rendering; they are built with F4 and slice 1, where positions first exist | `NeuronServer/TickClock.*`, ADR-007 | `TickClockTests` (five, including ten minutes exactly); `UniverseTests` unchanged and still exact |
| F3 | **Done, as a library** ([ADR-008](ADR/ADR-008-messages-encoding-and-the-in-process-transport.md)). The first four message kinds carry what the snapshot carries today: tick, ship, objective and resource. Every message is encoded to bytes even in process, behind an 8-byte header of catalogue version, kind and payload length. Each kind has a fixed route: reliable on a named stream, or a newest-wins datagram. `LoopbackTransport` lives in `NeuronCore`, where both halves can reach it, and refuses a datagram over 1,100 bytes, the payload a QUIC path can always carry. Commands up join with the slices that need them. Moving the executable's hand-over onto this path is part of F4 | `NeuronCore/WireFormat.*`, `NeuronCore/LoopbackTransport.*`, `GameCore/Messages.*`, ADR-008 | `WireFormatTests` (5), `LoopbackTransportTests` (3), `MessageTests` (9): round trips, byte order, refusals, routes, the datagram budget |
| F0 | **Done** ([measurement note](Measurements/SmokeBaseline.md)). Store today's smoke capture and report as the reference F4 is checked against: WARP proved byte-deterministic, so its capture is an exact reference | `Design/Measurements/` | Three WARP runs byte-identical; the hardware run recorded beside it |
| F4 | The server owns the world: it loads its own content from the asset root, creates world objects with stable identifiers, archetype, owner and pose, and publishes their state through an observer (AGENTS.md R25): one observer in single player, which passes every object, on the path that 100 players will use. Only the shape is built now: per-object messages leaving through the observer, and no message carrying the whole world. Interest, priority and budgets are added behind the observer in slice 10. The client builds its own model of the world from those messages (today's `UniverseSnapshot`, grown) and draws poses from it instead of from scene positions. Built in three parts, each reviewable on its own: | | |
| F4a | **Done.** The server loads its own content (`Scene.txt`, read by `GameLogic` through `GameCore`) and holds world objects with an `ObjectId`, content name, model, owner and a double-precision pose (ADR-009). The executable stops handing it the client's parsed scene | `GameLogic/Universe.*`, `GameCore/WorldObject.*`, `Outpost/Main.cpp` | Every scene instance becomes one object whose pose reproduces `ModelToWorld`; identifiers are stable across reloads; a broken scene is refused by the server with the file and line |
| F4b | **Done.** The observer and the per-object messages: `ObjectSpawn` (reliable, on the world stream, with the initial pose) and `ObjectState` (a newest-wins datagram); ships, objectives and resources keyed by object or index. The observer encodes from the world without changing it and sends only what changed since it last sent | `GameLogic/Observer.*`, `GameCore/Messages.*` | Every object reaches the client as its own messages; no message exceeds the datagram budget; a second publish with nothing changed sends nothing |
| F4c | **Done.** The client builds its own `UniverseSnapshot` from decoded messages and draws poses from it. The by-reference hand-over in `Main.cpp` goes; the client draws only objects it was told about | `GameCore/WorldSnapshot.*`, `GameApp/TacticalScene.*`, `GameApp/TacticalClient.*`, `Outpost/Main.cpp` | Applying the server's messages reproduces its state field by field; the WARP smoke capture matches F0's reference |
| F5 | **Done**, with one move: the script parser is `GameApp/InputScript.*`, where its tests can reach it. `--expect <report>` checks a run's image statistics against a stored report. The optional CI step is not added. Scripted smoke test: `--script <file>` plays a timeline of commands at fixed ticks and `--frames` runs past it. Optionally add a WARP smoke run to CI's Windows job once it is stable | `Outpost/CommandLine.*`, `Outpost/Main.cpp` | A script that orbits and selects reproduces a capture whose statistics match a stored report |
| F6 | **Done, verdict go** ([measurement note](Measurements/F6-RendererBaseline.md)). **F6a**, measure the baseline: frame time percentiles with `--no-vsync`, written to the smoke report; the time `ExtractSurface` takes for each shipped model and for a 16³ brick. **F6b**, a **crowd benchmark**, which is a go/no-go decision on the rendering technique. A generated scene at GameDesign.md §8.2's load (600 ships, 20 outposts with their claims) is drawn by today's renderer twice: at full detail, and with objects beyond a few hundred metres swapped for 2× and 4× downsampled copies from the generator, a crude stand-in for levels of detail. **Go:** the stand-in run, main pass and shadow pass included, fits in about 12 ms at 1920×1080 on the reference GPU, leaving the rest of the frame for effects and HUD; ADR-003's per-voxel technique stays. **No-go:** prototype the alternative, triangle meshes built per brick from the same masks, measure it on the same scene, and decide between them in an ADR that replaces ADR-003, before slice 1 builds on either | `Outpost/Main.cpp`, a benchmark mode in the smoke test, a crowd scene and downsampled models from `AssetGenerator`, which PR #3 has since removed (the crowd generator is in the history at `fff703f`) | Numbers recorded in a measurement note, with how they were taken; the go/no-go verdict cited by ADR-010 and ADR-013 |
| F7 | **Done** ([ADR-017](ADR/ADR-017-nuget-and-the-msquic-transport.md)). The tool is `Tools/QuicSpike/`, in the `Tools` solution folder ([ADR-025](ADR/ADR-025-tool-projects-under-tools.md)). A relay inside it induces the loss. It showed that state must be built only when the connection can send, and ADR-017 records that. The MsQuic spike, after F3: a console tool under `Tools\`, the first project to reference a NuGet package, `Microsoft.Native.Quic.MsQuic.Schannel`, (GameDesign.md §8.1). It sends the real encoded catalogue between two endpoints on localhost: one reliable stream, datagrams for state with sequence numbers, a self-signed certificate pinned on first connection, callbacks that only enqueue. It is not part of the game or the test suites, and it adds a row to AGENTS.md §2 and to `.clang-tidy`'s `HeaderFilterRegex` like any project | `Tools/QuicSpike/` (new), ADR-017 | Run by hand on Windows 11: every message kind crosses in its delivery class; every state message fits the datagram budget; with induced loss, state stays current and the stream stays ordered; a changed host certificate is refused. Results recorded in ADR-017 |

Content records (ADR-009) come with F4. `Scene.txt` stays the placement file; archetypes move into typed record files under `Assets/Content/` (ships, weapons, shields, modules, materials), read with `RecordFile`, with every reference checked at load (R24). The same ADR gives sockets a **class** (dock, module, equipment, clamp), which slice 2 needs for docking points and slice 4 for building.

## 5. Slice 1 — a ship that flies

**Goal.** An escort flown by cursor around the shipyard, with the flight model of the blueprint's chapter 02, a chase camera, and a key that switches to the tactical view, where the same ship obeys a go-to order.

**ADRs due:** ADR-010 (moving instances and the shadow map).

| # | Task | Where | Tests |
|---|---|---|---|
| 1.1 | **Done.** Ship archetype records: mass, inertia, hull drag, steering torque and angular drag per axis, strafe force and drain, maximum bank. Engine, power plant and thruster records. Values for the two escorts from the blueprint's first-tier light fighter | `Assets/Content/Ships.txt`, `GameCore/ShipArchetype.*` | Parsing, validation, every reference resolves |
| 1.2 | **Done.** The tick rate is a parameter, because `TickClock` is in `NeuronServer`, which the client may not reference. Zone drag waits for zones (slice 2), and refusing cruise near an obstacle waits for collisions (1.11). The flight model as pure code shared by both halves: linear and angular damping with the exponential form throughout, force controllers (engine, reverse, strafe, thruster, steering), the two pools, engine kill, the cruise state machine with charge, ramp and cooldown, the global limits | `GameCore/FlightModel.*` | Against chapter 02's worked example: top speed 80 m/s, 95 % in 0.5 s, stop in 13 m, thruster 200 m/s, strafe 33 m/s, pitch and yaw rate 92°/s, cruise 300 m/s reached 5 s plus about 3.7 s after engage, coasting speed halved in 69 s, thruster pool lasting about 15 s |
| 1.3 | **Done.** The manoeuvre layer, minimal: free flight fed by player input, hold position, go to a point or object with stand-off and arrival. The arbiter of chapter 02 §3.9 (reverse beats throttle, engine kill beats cruise) | `GameCore/Manoeuvre.*` | Go-to arrives within its stand-off from several start poses; free flight passes input through unchanged |
| 1.4 | **Done.** `GameLogic/Commands` takes the client's `FlightInput` messages in; a missing command repeats the last and marks the tick diverged, and the observer then sends the own ship's exact state (`OwnShip`). Each ship's motion state goes out as a newest-wins datagram. The server simulates ships: applies per-tick input commands to the player's ship and directives to the others, integrates every ship, publishes states | `GameLogic/Universe.*` | Scenario: an input timeline produces the same trajectory whatever the render frame rate (the client's frame count per tick varies, the ticks do not). `FlightTests`: 420 ticks at 30, 60 and 144 fps, bit-identical |
| 1.5 | **Done.** Escape became Cancel, and quitting moved to F10 (Alt+F4 still works). The command layer of chapter 09 §3.1: devices produce commands, contexts scope them, bindings in a record file. MainWindow forwards every key and mouse event instead of handling six keys itself | `NeuronClient/CommandMap.*`, `Outpost/MainWindow.cpp`, `Assets/Content/Bindings.txt` | Binding lookup; context precedence; a key held across a context switch releases cleanly |
| 1.6 | **Done.** The click radius that arms steering before the hold ends is a sixteenth of the window's width. Mouse flight: cursor offset to axis demand, the 0.25 s hold before steering, the lock toggle, rate control, banking into the turn; the cursor confined to the window while flying | `GameApp/FlightInput.*` | Demand from offset at screen centre, edge and corner; the hold threshold; banking scaled by throttle with the 0.4 floor |
| 1.7 | **Done** (ADR-010). A correction snaps the ship to the replayed state, with no smoothing; in loopback none happens. Own-ship prediction: the client runs the same flight model on the same input commands and draws its own ship from the prediction, interpolated between ticks; every other object is interpolated between received states | `GameApp/Prediction.*` | In loopback, prediction and server state are identical tick for tick. The smoke test also counts it on every run and fails, with exit code 10, on a tick that differs uncorrected |
| 1.8 | **Done.** The per-hull offsets are `chase` records in `Ships.txt`. The chase camera of chapter 09 §3.9: per-hull offset, speed lag, turn look-ahead, its own up vector, shake. Pure math beside `OrbitCamera` | `NeuronClient/ChaseCamera.*` | Offset at rest, lag under acceleration, look-ahead direction, levelling |
| 1.9 | **Done**, with the flight view in `TacticalClient` and its HUD in `GameApp/FlightHud.*` rather than a `FlightView` file. The class keeps the name `TacticalClient` until slice 2 reshapes it. A right click in the tactical view orders the ship. The flight view: the chase camera, the cursor confined while flying, and a minimal flight HUD (speed, throttle, both pools, cruise state). The tactical view becomes the docked view in slice 2 (D10); until then a debug-only key switches to it for development, and slice 2 removes the key | `GameApp/FlightView.*`, `GameApp/TacticalClient.*`, `GameApp/TacticalHud.*` | HUD layout tests at 720p, 1080p and 4K as for the tactical HUD |
| 1.10 | **Done** (ADR-010), with two tests not written. Picking a moving ship has no test: `PickShip` needs a scene built from GPU assets, which the test suites do not create. The flying smoke capture is `FlightSmoke.txt`, which flies past the yard and then gives an order, rather than across the hangar. Moving instances: transforms from the snapshot every frame; bounds and picking follow; engine plumes follow their ship and scale with throttle; the shadow map redrawn each frame over a shadow camera fitted to the play area (ADR-010), chosen so that it grows into cascades around the camera without being replaced (§8.2); every transform built relative to the camera, from double-precision positions | `GameApp/TacticalScene.*`, `GameApp/TacticalClient.cpp` | Picking a moving ship; the smoke capture with a script that flies a ship across the hangar; no visible jitter for a ship and camera 100 km from the origin |
| 1.11 | **Done.** A body is the box of its whole model grid, which is coarse: the Talon is parked 1.2 m from the dry dock's box. Cruise is refused while an immovable body lies within 600 m ahead (two seconds at cruise speed). Collisions, minimal: ship against the bounds of stations, rocks and ships, swept, with chapter 02's bounce and collision damage recorded but not yet applied | `GameCore/Collision.*`, `GameLogic/Universe.*` | A ship flown into the hangar wall stops outside it at every speed regime, cruise included |

**Choices made while building.** Four choices were made without the owner and then confirmed: the Lancer as the player's ship (GameDesign.md D14), and the cursor sensitivity, the Escape and F10 keys and the chase camera's offsets (D15).

**It works when** top speed and turn rate come out of thrust and drag as chapter 02 predicts (1.2's tests), an input timeline flies the same path at 30, 60 and 144 rendered frames a second (1.4), and another ship flies to a point under a go-to directive through the same manoeuvre layer (scenario test). By hand: flying feels immediate, and the frame budget holds with the shadow map redrawn every frame.

**Risks.** Redrawing a 4096² shadow map of every voxel each frame may not fit the budget (F6 will tell). Fallbacks in order: draw static structures into a cached map and only moving ships each frame; lower the resolution of a dynamic cascade; shadow moving ships only near the camera.

## 6. Slice 2 — a place to be

**Goal.** Dock at the shipyard and launch again; docked is the tactical view of the station; zones and one hazard; the game saves on launch and resumes from a save.

**ADRs due:** ADR-011 (the character record and save format), ADR-023 (the UI foundation: the DDS subset, atlas manifest, colour and alpha rules, and the HUD batch changes).

**Decided before it started** (GameDesign.md D16 to D20): a docked ship stays in its berth, protected; the game saves on launch, docking and quit into one slot, with no manual saves; start-up resumes the save, or starts a new game docked; pause does not exist; a Dock key asks the nearest station. The docked view is built on the UI foundation of [GameplayUiProposal.md](GameplayUiProposal.md), which comes first (task 2.5a, the owner's decision of 2026-11-22).

| # | Task | Where | Tests |
|---|---|---|---|
| 2.1 | Zones: sphere, box and cylinder; immersion with an edge fraction; "smallest zone wins"; property queries. A hazard zone with damage rate and scanner interference around the frigate's breach, a named region for the shipyard | `GameCore/Zone.*`, `Assets/Content/System.txt` | Chapter 01's immersion formula at the core, the edge and outside; nesting; overlap resolution |
| 2.2 | Docking points as `dock`-class sockets on `HangarBay` and `DockingCollar`, with a docking class on each hull | `Assets/Scene.txt` or `Assets/Content/`, `GameCore/SceneDefinition.*` | Validation: every dockable station has at least one point; classes resolve |
| 2.3 | The dock request on the authority with chapter 04's checks that apply without factions or missions: has docking points, a point accepts the class, a point is free, the target is at rest. Answers proceed, wait, cannot dock; the waiting list | `GameLogic/Docking.*` | Each check's refusal in order; two ships queue for one pad; the 90 s abandonment |
| 2.4 | The dock manoeuvre and the approach: go to the approach point, then a scripted mover along the point's approach path with collisions off; completion makes the ship docked, and it stays in its berth, protected (D16). The Dock key asks the nearest station within 10 km (D20). Launch plays it backwards with launch invulnerability | `GameCore/Manoeuvre.*`, `GameLogic/Universe.*` | Scenario: a docking from several start poses completes; launch places the ship clear and protected |
| 2.5a | The UI foundation, the proposal's step 1 (ADR-023): a bounded reader for 2D DX10 DDS files in the declared formats, static texture upload including block-compressed layouts, a named sprite manifest checked at load, coloured and mask sprites and nine-slice panels in the HUD batch beside today's R8 text path, and shared control states with input focus. The existing tactical HUD is re-skinned on it as the reference integration | `NeuronCore/` (the DDS reader), `NeuronClient/HudBatch.*`, `NeuronClient/Renderer.*`, `NeuronClient/Shaders/Hud*`, `Assets/Ui/` | Valid and truncated DDS files; supported and refused formats; compressed payload sizes; sprite references, atlas bounds and nine-slice margins; batch order with the thumbnails; the tactical HUD's layout tests still pass |
| 2.5 | Docked state on the client, built on 2.5a with the proposal's rail and inspector, the only place the tactical view exists (D10): the tactical view framed on the station, launching leaves it, and the slice-1 debug key goes, a services panel listing what the station's modules provide (in slice 2: launch and a placeholder repair), launch refused without engine or power plant | `GameApp/BaseView.*`, `GameApp/TacticalHud.*` | HUD layout at three resolutions; the launch refusal |
| 2.6 | The character record: location (docked at a base, or a pose in space), ship and fit, cargo, money, the base snapshot, interface state. Content appends its own sections through a save interface, not file handles (ADR-011) | `GameLogic/Character.*`, `GameLogic/SaveGame.*`, `NeuronServer/` for the file layer | Round trip of every field; an unknown section is kept on rewrite; a truncated file is refused with a reason |
| 2.7 | Saving as D17 and D18 say: saved on launch, on docking and on quit into one slot, the save written completely before it replaces the old one; start-up loads it, or starts a new game docked in the shipyard. No manual save and no pause panel (D19) | `GameLogic/SaveGame.*`, `Outpost/Main.cpp` | Scenario: launch, fly, dock, quit, restart, and the state is equal field by field; a new game starts docked |
| 2.8 | **Done**, on the client alone: `GameApp/HazardWarning.*` and the flight HUD. The warning is the presentation's (ch. 01 §3.11), worked out from the zones both halves load and where the own ship is drawn; a protected ship (D16) is warned of interference and not of damage. Scanner interference is a read-out until slice 3 (D21). `GameLogic/Universe.*` is untouched: the server accumulates zone damage with slice 3, when a hull has hit points for the one per cent threshold to mean anything. Hazard effects: accumulated zone damage applied as chapter 03 §3.7 says once slice 3 has damage; until then the HUD warning and scanner interference | `GameApp/HazardWarning.*`, `GameApp/FlightHud.*`, `GameApp/TacticalClient.*` | The warning appears inside the shipped breach and not outside; it weakens across the zone's edge; the HUD's warning stays on screen, clear of the panel and the reticle, at three resolutions |

**It works when** a full loop of launch, fly, dock and resume from a save passes as a scenario test and by hand, and the save file is a readable text record (ADR-011).

**Risks.** The approach path is new content per docking point. Keep it to an entry point, an approach point and the pad, authored as three sockets; the blueprint's three-pose approach needs nothing more.

## 7. Slice 3 — a fight in voxels

**Goal.** Guns, a shield, damage events that carve voxels, modules that fail, a core whose loss destroys the ship, wrecks; one opponent that attacks, evades and flees; the combat HUD. This is the slice with the most technical risk in the MVP, and nothing in slice 4 starts until a duel is good.

**ADRs due:** ADR-012 (the voxel mask and the damage event; R22 makes it due before the first line that carves), ADR-013 (changing surfaces in the renderer), ADR-014 (materials and module regions).

### The decisions this slice turns on

**The mask.** A damageable instance holds a bit per voxel of its archetype model: present or not. Colour and material come from the archetype, so a repair restores exactly what was there. The frigate's 48 × 150 × 40 grid is 36 KB of bits.

**The damage event** is what the authority sends and both halves apply: target identifier, impact cell in the target's VOX coordinates (integers), a shape from a short list (sphere, capsule along the shot) with integer radius, an energy budget in integer units, a cause and a seed. The authority does the only float work, turning a world-space hit into a VOX cell, once. Applying the event is integer-only: candidate cells in a fixed order, each removed while the energy left exceeds its material's strength, seeded rolls for a ragged edge. One function in `NeuronCore`, called by both halves (R22).

**Separation.** One function takes a mask and the cells an event removed, and returns the parts no longer attached to the core. In the MVP it does the simple thing: a fill from the core's cells. That is ample for a duel. At GameDesign.md §8.2's scale a full fill per hit would not survive hundreds of events a second, so after the MVP the same function becomes incremental (a graph of bricks and their contacts, a local fill only where a removal may have cut a bridge) without its callers changing. Anything no longer attached to the core becomes a wreck, a tick later: a new object with its own mask, the velocity of the ship plus a kick. A core with too few cells left stops, and the ship breaks up into wrecks.

**Surfaces.** Damageable models are stored as 16³ bricks from the start. Re-extracting a whole frigate per event might be fast enough for one duel, but GameDesign.md §8.2's battle produces hundreds of events a second. Events mark bricks dirty; each dirty brick and the neighbours whose exposure and occlusion it changes are re-extracted at most once a frame, nearest and largest on screen first, within a time budget. The mask is always current; only its drawing may lag. Bricks are also the unit the later levels of detail and GPU culling work on. F6's measurement sets the brick size and the per-frame budget, and ADR-013 records both. A damaged instance gets its own bricks on the GPU; undamaged instances keep sharing their model's.

| # | Task | Where | Tests |
|---|---|---|---|
| 3.1 | `VoxelMask`, the damage event, the carve, connectivity and separation, a mask hash | `NeuronCore/VoxelMask.*`, `NeuronCore/VoxelCarve.*` | The same event on two masks gives equal hashes; carve shapes; energy accounting; a cut through the frigate's midsection separates the bow; a cell order that does not depend on container iteration; a mask snapshot or brick diff named with its last event, applied and followed by later events, hashes equal to the mask that saw every event |
| 3.2 | Materials (strength, mass, yield) beside the emissive table; palette roles extended with an **ore** family now, shared by every model (ADR-002's consequence) | `GameCore/Palette.*`, all `Assets/Models/*.vox` | Palette test across every model |
| 3.3 | Module regions: named boxes in VOX coordinates with a function and two thresholds (reduced, stopped), one of them the core. Authored alongside the models and checked against their geometry | `Assets/Content/Modules.txt`, `GameCore/ModuleRegion.*` | Regions inside their model; exactly one core per ship; the frigate's authored damage reproduced from a fixture mask |
| 3.4 | Weapons: launcher records, the firing cycle with chapter 03's refusals in order, power per shot, bolts as traced segments with muzzle speed and lifetime, the lead point | `GameCore/Launcher.*`, `GameLogic/Combat.*` | Each refusal in order; the clock restarts on an attempt that fails for power; the lead point against a crossing target |
| 3.5 | Shields: capacity, regeneration drawing on the power pool, collapse at the threshold, the rebuild time, energy damage draining the pool, no bleed-through | `GameCore/Shield.*` | Chapter 03's state table; the worked example's shot count to collapse with its numbers |
| 3.6 | Hits on the authority: bolt against shield bubble, then against the target's voxels by DDA through the mask; a hit becomes a damage event; module health becomes the share of its cells present; functions degrade and stop at the thresholds (no engine cells, no thrust) | `GameLogic/Combat.*`, `NeuronCore/RayBox.*` | A bolt through a gap in a damaged hull passes through; an engine region carved away stops thrust |
| 3.7 | Death and D1: core stopped, breakup into wrecks, the player returned to the outpost with the base snapshot fit and no cargo; D2: an indestructible flag that the authority honours | `GameLogic/Universe.*`, `GameLogic/Character.*` | Scenario: a ship killed by carving its core; the player's return; the command block survives any event |
| 3.8 | The opponent: chapter 05's manoeuvre layer with attack pass, evade, trail and flee; a target chosen by an owner tag until factions exist; fire control with burst and accuracy from a pilot record; aim at a chosen module | `GameCore/Pilot.*`, `GameLogic/Ai.*`, `Assets/Content/Pilots.txt` | Behaviour vetoes; the pass rhythm (run in, pass, extend); accuracy cone against a player per pilot tier |
| 3.9 | The client: applies damage events through the same function, updates surfaces per ADR-013, plays hit sparks, breach embers and debris, draws wrecks; hostile models (the Raider VOX model) | `GameApp/`, `NeuronClient/Renderer.*`, `Assets/Models/Raider.vox` | Client and server masks hash equal after a scripted fight |
| 3.10 | The combat HUD: target brackets, shield and structure bars, lead marker, sub-targeting by module, the selected-ship panel's percentages measured from masks instead of read from `Scene.txt` | `GameApp/TacticalHud.*`, `GameApp/FlightView.*` | Layout at three resolutions; the panel's figures equal the mask's |
| 3.11 | Repair consumable: patches structure in the field, leaves modules for a hangar | `GameLogic/Combat.*` | It restores structural cells and no module cells |

**It works when** a scripted duel between the player's escort and a raider of the same tier, run headless, breaks the shield in roughly the blueprint's span (seconds, not tens of seconds), exposes the target for most of the rebuild window, and reaches the core within two or three windows; when client and server masks hash equal after it; and when, by hand, the frame budget holds while the frigate is carved and the outcome of a duel visibly depends on where the player cuts. The ship panel's percentages are measurements.

**Risks and what to do.** Carving cost: if the budget breaks, coarsen the carve (two-voxel cells for small arms), cap events applied per frame on the client (the authority is never capped), or move surface extraction to a worker thread with a one-frame delay. Readability: if fights become hard to read, give armour a stronger outer material so that early hits chip rather than gouge. Determinism: any hash mismatch is a release blocker, not a tuning issue.

## 8. Slice 4 — an outpost that grows

**Goal.** Sockets, build plans, Alloy, modules with services, power, crew and water, and repair. "Finish hangar extension F-03" is completed by building it.

**ADRs due:** ADR-015 (modules, construction and the outpost's rates), ADR-016 (the event bus and objectives).

| # | Task | Where | Tests |
|---|---|---|---|
| 4.1 | Module archetypes: function, power draw or supply, crew, storage, the services they provide, the sockets they bring with their classes, the Alloy each voxel costs | `Assets/Content/Modules.txt`, `GameCore/ModuleArchetype.*` | Validation; every socket class resolves |
| 4.2 | Editable module models: reactor, habitat, refinery, turret platform, shield generator, yard | `Assets/Models/` | VOX parsing; palette; size limits |
| 4.3 | The outpost on the authority: modules joined at sockets, a power pool, crew capacity, water consumption, shortage switching modules off in a stated priority order, never destroying them; all rates as integer amounts per tick with carried remainders, like today's income | `GameLogic/Outpost.*` | Exact consumption over a minute; shortage order; recovery when supply returns |
| 4.4 | Construction: a build command names a plan the player holds and a free socket that accepts it; the build plan appears as an object with an empty mask; Alloy is consumed as cells fill in a fixed order; the module starts working when complete and brings its sockets | `GameLogic/Construction.*`, `NeuronCore/VoxelMask.*` | Alloy consumed equals the module's cost exactly; a build stops when Alloy runs out and resumes; a refused socket |
| 4.5 | Repair at a hangar: restores a docked ship's mask and the station's own modules, consuming Alloy at the outpost; a module shot off takes its service with it and can be rebuilt | `GameLogic/Outpost.*` | Scenario: carve the hangar, lose repair; rebuild, regain it |
| 4.6 | The event bus of chapter 11 §3.5, typed: the authority announces events (module completed, module destroyed, ship destroyed, docked, launched), content listens in priority order. A minimal objective evaluator listens and completes objectives. It is the seed of slice 9's trigger language, not a substitute for it | `GameLogic/EventBus.*`, `GameLogic/Objectives.*` | Delivery order; a listener added during a broadcast joins after it; F-03 completes on its module's completion |
| 4.7 | The client: BUILD tab interactive (pick a plan, see accepting sockets, place), build plans drawn as today's wireframe filling with solid voxels as they are built, D5's rename of DESIGN to YARD, resources read from outpost state; `perminute` removed from `Scene.txt` | `GameApp/TacticalHud.*`, `GameApp/TacticalScene.*`, `GameCore/SceneDefinition.*` | HUD layout; the build panel's slots match held plans |

**It works when** F-03 is completed by building it, a module shot off takes its service with it and comes back when rebuilt, and an hour of simulated outpost running consumes exactly what the rates say (scenario test).

**Risks.** Construction animates a mask on the client every tick it grows. Fill in cell batches large enough that surface updates stay within ADR-013's cost.

## 9. Slice 5 — mining and haulage

**Goal.** Rock carved into ore, a tractor, miner and tug itineraries, a refinery, and the ledger. The outpost sustains itself from its claim.

**ADRs due:** none new; ADR-011 is amended for saving the claim asteroid's mask.

| # | Task | Where | Tests |
|---|---|---|---|
| 5.1 | The claim: `AsteroidMajor` as an authored solar whose mask is saved (run-length encoded; it is 2.6 million cells), with ore-family voxels painted in seams | `Assets/Models/AsteroidMajor.vox`, `GameLogic/Character.*` | Save round trip of a mined claim; encoded size recorded |
| 5.2 | Surfaces at that size: the claim almost certainly needs bricks even if ships do not (ADR-013 decides) | `GameApp/`, `NeuronCore/` | Carve-to-visible within budget on the claim |
| 5.3 | The mining tool: a launcher whose energy counts fully against rock and weakly against other materials; carving an ore cell yields Ore by the material's yield | `Assets/Content/Weapons.txt`, `GameLogic/Combat.*` | Yield per event is exact and the same on every run |
| 5.4 | Cargo and loot: the hold with volume, loot containers, the tractor of chapter 03 §3.9 with its tests and refusals | `GameCore/Cargo.*`, `GameLogic/Loot.*` | Each refusal reason; partial transfer when the hold is nearly full |
| 5.5 | Itineraries: a miner mines the claim until full and unloads at the outpost; a tug carries containers between the claim and the outpost. Built on the manoeuvre layer and docking | `GameLogic/Itinerary.*`, `GameCore/Manoeuvre.*` | Scenario: a miner completes ten cycles unattended; a tug's cycle survives a blocked pad |
| 5.6 | The refinery turns Ore into Alloy at a rate, drawing power | `GameLogic/Outpost.*` | Exact conversion over time; it stops without power or ore |
| 5.7 | The ledger: the same production, consumption and haulage rates computed for a system that is not running. The MVP has one system, so this is exercised by unloading it in a test, not by the player | `GameLogic/Ledger.*` | Scenario: an hour present and an hour by ledger end within haulage rounding of each other, and the ledger never ends ahead |
| 5.8 | The client: tractor beams, cargo panel, miner and tug status in the FLEET tab, ore seams readable on the rock | `GameApp/` | Layout tests |

**It works when** the outpost sustains Alloy and Water from its claim at the rate its modules and assignments imply, the same whether simulated or by ledger, and the player can mine by hand faster than a miner but not indefinitely (hold size and trips are the limit, as weakness 5 of the design requires).

## 10. Slice 6 — a wing

**Goal.** The yard builds escorts; formations; wing commands and tactical orders; raids. The three HUD objectives can all be completed or failed. This is the design's checkpoint.

**ADRs due:** none new, unless raids need an encounter record format that ADR-009 did not foresee.

| # | Task | Where | Tests |
|---|---|---|---|
| 6.1 | Owned ships: an NPC ship definition (archetype, loadout, pilot) with the player as owner, saved in the character record; crew capacity caps the fleet (D12) | `GameLogic/Fleet.*`, `GameLogic/Character.*` | Save round trip; the cap |
| 6.2 | The yard: builds a hull the player holds the plan for, consuming Alloy and time; refits under the hardpoint rules; the YARD tab | `GameLogic/Yard.*`, `GameApp/TacticalHud.*` | Build cost and time exact; a refit that breaks the class rules is refused |
| 6.3 | Formations of chapter 05 §3.10: slots, follow, cruise together, break and regroup | `GameCore/Formation.*`, `GameLogic/Ai.*` | Followers hold slots through a turn; regroup after a break |
| 6.4 | Wing commands in the flight view (form up, attack my target, defend, dock, hold fire) and, while docked, orders in the tactical view to any owned ship in the system (multiple selection, go to, attack, defend, dock, join formation, assign itinerary), all issued as directives | `GameApp/FlightView.*`, `GameApp/TacticalClient.*`, `GameLogic/Universe.*` | Each order becomes the right directive; box selection |
| 6.5 | Raids: an encounter record, an "assault this solar" itinerary, spawn around the present player from the claim's zone, a battle cap and a relief time (chapter 06) | `Assets/Content/Encounters.txt`, `GameLogic/Population.*` | Raid size from the zone; no second wave inside the relief time |
| 6.6 | Outpost defence: turret platforms and the shield generator as equipment on sockets, flown by a pilot record; a garrison on a defend directive | `GameLogic/Combat.*`, `GameLogic/Ai.*` | Turrets fire at hostiles only; a stripped turret stops |
| 6.7 | The scenario: the three objectives as an authored opening, using the event bus and objective evaluator: a raid to defend against, F-03 to build, T-041 arriving with an ambush on its way to dock; failure when the tug dies | `Assets/Content/Opening.txt`, `GameLogic/Objectives.*` | Scenario: each objective completed; the tug's loss fails its objective; the opening survives save and reload at any point |
| 6.8 | The fly-or-command dial: with the tactical view only in a berth, the choice is docking or launching. Pilot accuracy, the wing's strength and the outpost's own defences are tuned so that neither staying docked and commanding nor launching wins a raid reliably on its own | `Assets/Content/Pilots.txt`, `Assets/Content/Encounters.txt` | Headless: the same raid run with the player commanding from the berth, and with the player's scripted flying plus the wing, both succeed less than always and better together than either alone |

**It works when** "defend the shipyard" can be won from the cockpit and, docked at the outpost, from the tactical view, "escort tug T-041 to dock" can be lost, and all three objectives complete in one session that survives a save and reload.

**The checkpoint.** At the end of slice 6, play the MVP for an hour and answer the design's §12 honestly: is the scene worth playing, do the two views both earn their place, does the outpost pull the player back too strongly? The answer decides whether slices 7 to 10 start as designed, start changed, or do not start.

## 11. What the MVP stubs

| Missing system | Stand-in in the MVP | Replaced in |
|---|---|---|
| Factions and regard | An owner tag: the player's, hostile, neutral | Slice 7 |
| Population | Raid encounters only; the tug and miners are owned | Slice 7 |
| Markets, money, worth, level | The player starts with plans and Alloy; money exists in the record and nothing spends it | Slice 8 |
| Other systems, lanes, jumps, map | One system | Slice 8 |
| Trigger language, campaign, job board | The event bus with a minimal objective evaluator and one authored opening | Slice 9 |
| Network transport | The in-process transport over encoded messages, each with its MsQuic delivery class already declared | Slice 10, as MsQuic (ADR-017) |

## 12. Decisions to write as ADRs

| ADR | Subject | Due before | The question it answers |
|---|---|---|---|
| 007 | The simulation tick, time and position | F2 | The tick rate, how render frames relate to ticks, interpolation; double-precision positions and camera-relative rendering |
| 008 | Commands, state, observers and the in-process transport | F3 | What travels up and down, encoding with kind, length and version, ordering per stream and per object, who pumps, each message's delivery class over MsQuic, and the observer path of R25 |
| 009 | Content records and identifiers | F4 | Which record files, typed fields, reference checking, socket classes, how `Scene.txt` divides |
| 010 | Moving instances and the shadow map | Slice 1 | Per-frame transforms, shadow camera and redraw policy, and how it grows into cascades, with the F6 measurements |
| 023 | The UI foundation | Slice 2 (task 2.5a) | The DDS subset read and its validation, the atlas manifest's records, colour space and straight alpha, one mip level, and how coloured sprites and nine-slice panels join the HUD batch (GameplayUiProposal.md). Numbered after 021 and 022, which asset work took |
| 024 | Assets deployed by a post-build copy | Taken during slice 2, at the owner's request | One post-build event mirrors `Assets\` into the output directory in both configurations; asset files stop being project items. Visual Studio's up-to-date check no longer sees an asset-only edit |
| 011 | The character record and save format | Slice 2 | Sections, content's save interface, atomic replacement, mask encoding |
| 012 | The voxel mask and the damage event | First carve (slice 3) | The event's fields, integer application, cell order, the separation function's contract (a full fill behind it in the MVP), snapshots and brick diffs named by event (R22) |
| 013 | Changing surfaces in the renderer | Slice 3 | Brick size, the per-frame re-extraction budget and its priority, GPU buffers per damaged instance, the upload path, and room for levels of detail, with the measurements behind them |
| 014 | Materials and module regions | Slice 3 | Material fields, the ore family, regions paired with the editable models |
| 015 | Modules, construction and outpost rates | Slice 4 | Module records, build order of cells, integer rates and shortage priority |
| 016 | The event bus and objectives | Slice 4 | Event types, priority order, re-entrancy, the objective evaluator's limits |
| 017 | NuGet packages and the network transport over MsQuic | F7 (the spike), long before slice 10 | How any package is referenced and restored, in projects and in CI; which project references the MsQuic package and at which pinned version; run-time loading or linking, so that single player and tests never load it; the restore step in CI; the exception to ADR-005 for the package's imported build files; streams and datagrams per message class; certificate pinning and account authentication; how worker-thread callbacks reach the tick. The Schannel build, NuGet and pinning are already decided (GameDesign.md §8.1) |

## 13. Order and dependencies

```text
F6a frame timing ── F6b crowd benchmark, go/no-go ─────────────────────┐   (gates ADR-010, ADR-013, task 1.10)
F0 baseline ── ADR-009 ── F4a ── F4b ── F4c ── F5 scripted smoke test ──┼── slice 1 ── slice 2 ── slice 3 ── slice 4 ── slice 5 ── slice 6
                ADR-009 ── tasks 1.1 to 1.3 (pure GameCore code) ───────┘
F3 messages ── ADR-017 ── F7 MsQuic spike: alongside slice 1, before the catalogue grows in slice 2
F1 checkers: alongside everything
```

The renderer track (F6) and the world track (F0, ADR-009, F4) do not depend on each other and run side by side; F6 starts first, because its verdict can replace the technique that slice 1's rendering builds on.

Slice 3 needs slice 2's save (task 2.6) for D1's return to base. Its palette change (task 3.2) updates the embedded palettes, so it lands before any new model of slices 4 and 5. Inside a slice, the server-side rules and their tests come first, then the client. F1 can run alongside everything else. F7 does not gate slice 1, but its findings about delivery classes and the datagram budget should reach the catalogue before slices 2 and 3 add most of its messages. F6 must finish before ADR-010 and ADR-013 are written, because both are decided by its numbers, and before slice 1's rendering work starts, because a no-go replaces the technique that work builds on.

## 14. Toward 100 players

GameDesign.md §8.2 sets the target of 100 players in one scene. The MVP is single-player, so it builds the parts that are cheap now and expensive to retrofit, and leaves the rest to later slices with the decisions they need.

**What the MVP does now for that reason.** Only what would mean rewriting core structures or every system if left until later. Every item here delays slice 3, the duel the design calls its biggest risk, so the list is kept short on purpose:

| Task | What it secures |
|---|---|
| F2, F4 | Double-precision positions, so a 100 km system is as precise at its rim as at its centre (decided in F2, built in F4) |
| F3 | Delivery classes per message: input as redundant datagrams, damage ordered per object, state newest-wins; the datagram budget |
| F4 | The observer path, already used by single player: per-object messages, no whole-world message (R25) |
| F6 | The crowd benchmark as a go/no-go on the rendering technique, before six slices of rendering work build on it |
| F7 | MsQuic proven with the real catalogue while it is still small |
| 1.10 | Camera-relative transforms; a shadow approach that grows into cascades |
| 3.1 | The separation function's contract, so a faster algorithm can replace the MVP's later; snapshots and brick diffs named by event (R22) |
| ADR-013 | Bricks as the unit of drawing, re-extraction and, later, detail and culling |

**What comes after the MVP**, with its decision. Each of these is added behind an interface the MVP already has:

| Work | Decision | Needed by |
|---|---|---|
| Voxel levels of detail per brick, GPU culling of bricks against the view and occlusion, indirect draws | ADR-018 | Slice 7, when population and more outposts raise the load |
| Cascaded shadows with cached static casters; clustered lights; GPU particle pools | Within ADR-018 or its own | Slice 7 |
| Per-zone population and battle caps instead of per-player ones (§8.2) | Design, then the population ADR | Slice 7 |
| Incremental connectivity behind the separation function (§8.2) | ADR-012, amended | Slice 7, or earlier if raids on the outpost show the cost |
| Interest, priority and per-client budgets with delta compression against acknowledged state; coalesced brick diffs for distant observers; progressive streaming of outposts on join | ADR-019 | Slice 10 |
| A pose history for every object, and lag compensation for bolts against it with a bounded rewind | ADR-020 | Slice 10 |
| A headless server executable | ADR-019 or its own | Slice 10 |
| A 100-bot load test over MsQuic against §8.2's budgets | — | The gate for slice 10 |

## 15. Basis

The code as of commit `d331bcf`: `Outpost/Main.cpp`, `Outpost/MainWindow.h`, `NeuronServer/TickClock.h`, `GameLogic/World.*`, `GameCore/WorldSnapshot.h`, `GameCore/SceneDefinition.h`, `GameCore/Palette.h`, `NeuronCore/VoxModel.h`, `NeuronCore/VoxelMesher.h`, `NeuronCore/RecordFile.h`, `NeuronCore/ModelAxes.h`, `NeuronClient/Renderer.h`, `NeuronClient/GpuTypes.h`, `NeuronClient/OrbitCamera.h`, `GameApp/TacticalClient.*`, `GameApp/TacticalScene.h`, `GameApp/SceneAssets.h`, `.github/workflows/build.yml`, and the model sizes in the `.vox` files. The design: [GameDesign.md](GameDesign.md), including §8.1 on MsQuic and §8.2 on scale. The Frontier blueprint's chapters 01 to 06, 09 and 11, for every rule and number cited.

Nothing here was built or measured. Every figure in this plan is the blueprint's or arithmetic on file sizes, and the choices marked "recommendation" are for the ADRs to confirm or overturn with measurements.
