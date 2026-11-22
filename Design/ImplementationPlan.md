# Outpost:Frontier — MVP implementation plan

**Status:** plan for the owner's review · **Date:** 2026-11-22

This plan turns the first six slices of [GameDesign.md](GameDesign.md) §11 into work: what to build, in which project, behind which decision, with which tests, and how to tell a slice is finished. It assumes the code as it stands at commit `d331bcf` and the rules in [AGENTS.md](../AGENTS.md), which it does not repeat. It gives no time estimates; the order and the dependencies are the plan.

## 1. What the MVP is

**The MVP is slices 1 to 6 of the design: one playable scene.** The sample shipyard, in its one system, becomes a game. The player flies an escort by cursor, docks and launches, fights in voxels, builds the hangar extension, mines the asteroid and hauls ore, commands a wing, and survives a raid. The three objectives on the HUD today become things that can be completed, failed and saved:

| Objective | Completed by | Slice that makes it real |
|---|---|---|
| Defend the shipyard | Destroying or driving off a raid, from the cockpit or the tactical view | 6 (raid), on 3 (combat) |
| Finish hangar extension F-03 | Building the planned `HangarSection` on the `ExtensionDock` socket | 4 |
| Escort tug T-041 to dock | The tug reaching its docking point alive through an ambush | 6, on 2 (docking) and 5 (tug itinerary) |

**Not in the MVP:** factions and regard, ambient population beyond raids, markets, worth and level, a second system, lanes, jumps, routes, the map, the job board, the trigger language and the campaign, multiplayer transport. Section 11 lists what the MVP stubs in their place.

**Decisions this plan takes as settled**, recorded in GameDesign.md §13 by the same commit:

| # | Decision | What it does to the MVP |
|---|---|---|
| D1 | Death returns the player to the last base with the ship as fitted there; cargo is lost | Slice 3 builds the base snapshot and the return. In the MVP the last base is the outpost |
| D2 | The command block cannot be destroyed | Slice 3 gives the authority an indestructible flag; slice 6 raids can strip everything else |
| D5 | The DESIGN tab becomes YARD | Renamed in slice 4, filled in slice 6 |
| D6 | Only fighters and freighters are flyable | The two escort hulls are the flyable ships of the MVP; frigate, tug and miners are commanded |
| D12 | A wing of about three, capped by crew | Slice 6 |

## 2. Where the code starts

The sample is a sound base, but it was built as a still picture. These are the facts about it that shape the plan.

| Fact | Where | Consequence |
|---|---|---|
| The server does not know where anything is. `World` holds ship health, objectives and resources; positions live only in the client's `SceneDefinition` | `GameLogic/World.*`, `GameCore/WorldSnapshot.h` | Before anything moves, the server must own world objects with identities and poses (F4) |
| The server is loaded from the scene the client parsed: `world.Load(session->client.Scene())` | `Outpost/Main.cpp` | Allowed by R18 (a `GameCore` type handed over by the executable), but the server should load its own content (F4) |
| The snapshot is passed by reference, once per frame | `Outpost/Main.cpp` | A message flow replaces it (F3) |
| The tick is 20 Hz | `NeuronServer/TickClock.h` | Too coarse for cursor flight without prediction; ADR-007 sets the rate |
| Every instance of a model shares one `LoadedModel` and one GPU buffer | `GameApp/SceneAssets.*` | Damage needs a surface per damaged instance (slice 3) |
| The shadow map is drawn once; the shadow camera is fitted to the static scene | `GameApp/TacticalClient.cpp`, `TacticalScene::BuildShadowCamera` | Moving ships need it redrawn (slice 1) |
| Instance transforms are computed once in `TacticalScene::Build` | `GameApp/TacticalScene.cpp` | Poses must come from the snapshot every frame (slice 1) |
| Input is mouse orbit, pan, zoom and select, and six keys | `Outpost/MainWindow.cpp`, `GameApp/TacticalClient.*` | A command layer is needed before flight (slice 1) |
| HUD content (objectives, resources, build items) comes from `Scene.txt`; resources grow by an authored `perminute` | `Assets/Scene.txt`, `GameLogic/World.cpp` | Replaced by outpost state in slice 4 |
| The three checkers AGENTS.md relies on do not exist; CI skips their gates | `Build/` (absent), `.github/workflows/build.yml` | Written first (F1) |
| No uncapped frame time has ever been measured (ADR-003) | — | A baseline is measured before the renderer changes (F6) |

## 3. How every slice is verified

The same four kinds of evidence, every slice. A slice is not finished until all four exist for what it adds.

1. **Unit tests** in the suite of the library that owns the code, through its public headers, without a GPU (ADR-006).
2. **Scenario tests** in `GameLogicTests`: a `World` built from shipped or fixture content, driven by a list of commands for a fixed number of ticks, with the outcome asserted. Each slice's exit criterion is written as one of these where it can be, so that "it works when" is a test and not an opinion.
3. **The smoke test**, extended with `--script <file>`: a timeline of input commands played at fixed ticks, so the executable can be driven through flight, docking and a fight on WARP and its capture checked (F5).
4. **A run by hand** on hardware, as AGENTS.md requires for anything that touches presentation, with frame time read from the status line.

**Performance budget, MVP-wide:** a frame at 1920×1080, Release, on the reference GPU (the RTX 3070 Ti Laptop the README names), stays under 16.7 ms with v-sync off, including the frame in which voxels are carved. A carve is visible in the frame after the tick that issued it. WARP is a correctness gate, never a performance one.

**Determinism check, from slice 3:** every scenario test that carves voxels also runs the client's application of the same damage events and asserts that the two masks hash equal (R22).

## 4. Foundations (before slice 1)

Six pieces of work that every slice stands on. None of them is visible to a player.

**ADRs due:** ADR-007, ADR-008, ADR-009 (section 10).

| # | Task | Where | Tests |
|---|---|---|---|
| F1 | Write the three checkers AGENTS.md names: `Build/CheckFormat.py` (clang-format over the tree, `--fix`), `Build/CheckProjectFiles.py` (build shape, project registration, R2 affixes, R7 file names, R11 spellings, flat directories, functional filters, Debug/Release alignment), `Build/RunClangTidy.py`. CI picks them up through its existing guards | `Build/` | Each checker passes over today's tree and fails on a planted violation of every rule it claims |
| F2 | Set the simulation tick per ADR-007 and give every rule a tick-based time type. Recommendation: 60 Hz for both halves in the MVP. The blueprint's authority ran at 30 Hz. At 60 Hz cursor flight needs no latency hiding, and the fastest response the blueprint authors (a fighter's roll, 0.03 s) stays longer than one tick; with the exponential form of the damping every rate is stable, so the choice is about feel and cost, and the cost of a few dozen ships is small | `NeuronServer/TickClock.h`, `GameLogic/World.cpp` | `TickClockTests` and `WorldTests` updated; income per minute still exact |
| F3 | The message catalogue and the in-process transport per ADR-008: commands up (player input per tick, directives, requests), state down (object states, discrete events), handed over by `Outpost.exe`. Recommendation: messages are encoded to bytes even in process, so that a pointer or a reference cannot cross by accident and slice 10 is a socket, not a rewrite | `GameCore/Messages.*`, `GameCore/MessageCodec.*`, `NeuronServer/Transport.*`, `Outpost/Main.cpp` | Round trip of every message kind; the transport preserves order and drops nothing; the encoder rejects an unknown kind |
| F4 | The server owns the world: it loads its own content from the asset root, creates world objects with stable identifiers, archetype, owner and pose, and publishes their state. `WorldSnapshot` carries object states; the client draws poses from it instead of from scene positions | `GameLogic/World.*`, `GameCore/WorldSnapshot.*`, `GameApp/TacticalScene.*` | The first snapshot reproduces every scene pose; identifiers are stable across reloads; the smoke capture is unchanged against today's reference |
| F5 | Scripted smoke test: `--script <file>` plays a timeline of commands at fixed ticks and `--frames` runs past it. Optionally add a WARP smoke run to CI's Windows job once it is stable | `Outpost/CommandLine.*`, `Outpost/Main.cpp` | A script that orbits and selects reproduces a capture whose statistics match a stored report |
| F6 | Measure the baseline: frame time percentiles with `--no-vsync`, written to the smoke report; the time `ExtractSurface` takes for each shipped model | `Outpost/Main.cpp`, a benchmark mode in the smoke test | Numbers recorded in ADR-009 or a measurement note, with how they were taken |

Content records (ADR-009) come with F4. `Scene.txt` stays the placement file; archetypes move into typed record files under `Assets/Content/` (ships, weapons, shields, modules, materials), read with `RecordFile`, with every reference checked at load (R24). The same ADR gives sockets a **class** (dock, module, equipment, clamp), which slice 2 needs for docking points and slice 4 for building.

## 5. Slice 1 — a ship that flies

**Goal.** An escort flown by cursor around the shipyard, with the flight model of the blueprint's chapter 02, a chase camera, and a key that switches to the tactical view, where the same ship obeys a go-to order.

**ADRs due:** ADR-010 (moving instances and the shadow map).

| # | Task | Where | Tests |
|---|---|---|---|
| 1.1 | Ship archetype records: mass, inertia, hull drag, steering torque and angular drag per axis, strafe force and drain, maximum bank. Engine, power plant and thruster records. Values for the two escorts from the blueprint's first-tier light fighter | `Assets/Content/Ships.txt`, `GameCore/ShipArchetype.*` | Parsing, validation, every reference resolves |
| 1.2 | The flight model as pure code shared by both halves: linear and angular damping with the exponential form throughout, force controllers (engine, reverse, strafe, thruster, steering), the two pools, engine kill, the cruise state machine with charge, ramp and cooldown, the global limits | `GameCore/FlightModel.*` | Against chapter 02's worked example: top speed 80 m/s, 95 % in 0.5 s, stop in 13 m, thruster 200 m/s, strafe 33 m/s, pitch and yaw rate 92°/s, cruise 300 m/s reached 5 s plus about 3.7 s after engage, coasting speed halved in 69 s, thruster pool lasting about 15 s |
| 1.3 | The manoeuvre layer, minimal: free flight fed by player input, hold position, go to a point or object with stand-off and arrival. The arbiter of chapter 02 §3.9 (reverse beats throttle, engine kill beats cruise) | `GameCore/Manoeuvre.*` | Go-to arrives within its stand-off from several start poses; free flight passes input through unchanged |
| 1.4 | The server simulates ships: applies per-tick input commands to the player's ship and directives to the others, integrates every ship, publishes states | `GameLogic/World.*` | Scenario: an input timeline produces the same trajectory whatever the render frame rate (the client's frame count per tick varies, the ticks do not) |
| 1.5 | The command layer of chapter 09 §3.1: devices produce commands, contexts scope them, bindings in a record file. MainWindow forwards every key and mouse event instead of handling six keys itself | `NeuronClient/CommandMap.*`, `Outpost/MainWindow.cpp`, `Assets/Content/Bindings.txt` | Binding lookup; context precedence; a key held across a context switch releases cleanly |
| 1.6 | Mouse flight: cursor offset to axis demand, the 0.25 s hold before steering, the lock toggle, rate control, banking into the turn; the cursor confined to the window while flying | `GameApp/FlightInput.*` | Demand from offset at screen centre, edge and corner; the hold threshold; banking scaled by throttle with the 0.4 floor |
| 1.7 | Own-ship prediction: the client runs the same flight model on the same input commands and draws its own ship from the prediction, interpolated between ticks; every other object is interpolated between received states | `GameApp/Prediction.*` | In loopback, prediction and server state are identical tick for tick |
| 1.8 | The chase camera of chapter 09 §3.9: per-hull offset, speed lag, turn look-ahead, its own up vector, shake. Pure math beside `OrbitCamera` | `NeuronClient/ChaseCamera.*` | Offset at rest, lag under acceleration, look-ahead direction, levelling |
| 1.9 | The flight view and the view toggle: one key switches between chase and orbit camera without pausing; in the tactical view the own ship holds or follows its last directive. A minimal flight HUD: speed, throttle, both pools, cruise state | `GameApp/FlightView.*`, `GameApp/TacticalClient.*`, `GameApp/TacticalHud.*` | HUD layout tests at 720p, 1080p and 4K as for the tactical HUD |
| 1.10 | Moving instances: transforms from the snapshot every frame; bounds and picking follow; engine plumes follow their ship and scale with throttle; the shadow map redrawn each frame over a shadow camera fitted to the play area (ADR-010) | `GameApp/TacticalScene.*`, `GameApp/TacticalClient.cpp` | Picking a moving ship; the smoke capture with a script that flies a ship across the hangar |
| 1.11 | Collisions, minimal: ship against the bounds of stations, rocks and ships, swept, with chapter 02's bounce and collision damage recorded but not yet applied | `GameCore/Collision.*`, `GameLogic/World.*` | A ship flown into the hangar wall stops outside it at every speed regime, cruise included |

**It works when** top speed and turn rate come out of thrust and drag as chapter 02 predicts (1.2's tests), an input timeline flies the same path at 30, 60 and 144 rendered frames a second (1.4), and in the tactical view the same ship flies to a clicked point (scenario plus a run by hand). By hand: flying feels immediate, and the frame budget holds with the shadow map redrawn every frame.

**Risks.** Redrawing a 4096² shadow map of every voxel each frame may not fit the budget (F6 will tell). Fallbacks in order: draw static structures into a cached map and only moving ships each frame; lower the resolution of a dynamic cascade; shadow moving ships only near the camera.

## 6. Slice 2 — a place to be

**Goal.** Dock at the shipyard and launch again; docked is the tactical view of the station; zones and one hazard; the game saves on launch and resumes from a save.

**ADRs due:** ADR-011 (the character record and save format).

| # | Task | Where | Tests |
|---|---|---|---|
| 2.1 | Zones: sphere, box and cylinder; immersion with an edge fraction; "smallest zone wins"; property queries. A hazard zone with damage rate and scanner interference around the frigate's breach, a named region for the shipyard | `GameCore/Zone.*`, `Assets/Content/System.txt` | Chapter 01's immersion formula at the core, the edge and outside; nesting; overlap resolution |
| 2.2 | Docking points as `dock`-class sockets on `HangarBay` and `DockingCollar`, with a docking class on each hull | `Assets/Scene.txt` or `Assets/Content/`, `GameCore/SceneDefinition.*` | Validation: every dockable station has at least one point; classes resolve |
| 2.3 | The dock request on the authority with chapter 04's checks that apply without factions or missions: has docking points, a point accepts the class, a point is free, the target is at rest. Answers proceed, wait, cannot dock; the waiting list | `GameLogic/Docking.*` | Each check's refusal in order; two ships queue for one pad; the 90 s abandonment |
| 2.4 | The dock manoeuvre and the approach: go to the approach point, then a scripted mover along the point's approach path with collisions off; completion makes the ship docked. Launch plays it backwards with launch invulnerability | `GameCore/Manoeuvre.*`, `GameLogic/World.*` | Scenario: a docking from several start poses completes; launch places the ship clear and protected |
| 2.5 | Docked state on the client: the tactical view framed on the station, a services panel listing what the station's modules provide (in slice 2: launch and a placeholder repair), launch refused without engine or power plant | `GameApp/BaseView.*`, `GameApp/TacticalHud.*` | HUD layout at three resolutions; the launch refusal |
| 2.6 | The character record: location (docked at a base, or a pose in space), ship and fit, cargo, money, the base snapshot, interface state. Content appends its own sections through a save interface, not file handles (ADR-011) | `GameLogic/Character.*`, `GameLogic/SaveGame.*`, `NeuronServer/` for the file layer | Round trip of every field; an unknown section is kept on rewrite; a truncated file is refused with a reason |
| 2.7 | Saving as the design says: autosave on launch, manual save only while docked, load on start; the save written completely before it replaces the old one | `GameLogic/SaveGame.*`, `Outpost/Main.cpp`, `GameApp/` pause panel | Scenario: launch, fly, dock, save, reload, and the state is equal field by field |
| 2.8 | Hazard effects: accumulated zone damage applied as chapter 03 §3.7 says once slice 3 has damage; until then the HUD warning and scanner interference | `GameLogic/World.*`, `GameApp/` | The warning appears inside the zone and not outside |

**It works when** a full loop of launch, fly, dock and resume from a save passes as a scenario test and by hand, and the save file is a readable text record (ADR-011).

**Risks.** The approach path is new content per docking point. Keep it to an entry point, an approach point and the pad, authored as three sockets; the blueprint's three-pose approach needs nothing more.

## 7. Slice 3 — a fight in voxels

**Goal.** Guns, a shield, damage events that carve voxels, modules that fail, a core whose loss destroys the ship, wrecks; one opponent that attacks, evades and flees; the combat HUD. This is the slice with the most technical risk in the MVP, and nothing in slice 4 starts until a duel is good.

**ADRs due:** ADR-012 (the voxel mask and the damage event; R22 makes it due before the first line that carves), ADR-013 (changing surfaces in the renderer), ADR-014 (materials and module regions).

### The decisions this slice turns on

**The mask.** A damageable instance holds a bit per voxel of its archetype model: present or not. Colour and material come from the archetype, so a repair restores exactly what was there. The frigate's 48 × 150 × 40 grid is 36 KB of bits.

**The damage event** is what the authority sends and both halves apply: target identifier, impact cell in the target's VOX coordinates (integers), a shape from a short list (sphere, capsule along the shot) with integer radius, an energy budget in integer units, a cause and a seed. The authority does the only float work, turning a world-space hit into a VOX cell, once. Applying the event is integer-only: candidate cells in a fixed order, each removed while the energy left exceeds its material's strength, seeded rolls for a ragged edge. One function in `NeuronCore`, called by both halves (R22).

**Separation.** After an event, a flood fill from the core module's cells finds what is still attached. Anything not attached becomes a wreck: a new object with its own mask, the velocity of the ship plus a kick. A core with too few cells left stops, and the ship breaks up into wrecks.

**Surfaces.** Measure first (F6): if `ExtractSurface` on the whole frigate takes well under a millisecond, re-extract a damaged instance whole and re-upload its buffer. If not, split damageable models into 16³ bricks and re-extract only dirty bricks and their neighbours, since a voxel's exposure and occlusion depend on cells next to it. ADR-013 records the measurement and the choice. Either way, a damaged instance gets its own surface and GPU buffer; undamaged instances keep sharing their model's.

| # | Task | Where | Tests |
|---|---|---|---|
| 3.1 | `VoxelMask`, the damage event, the carve, connectivity and separation, a mask hash | `NeuronCore/VoxelMask.*`, `NeuronCore/VoxelCarve.*` | The same event on two masks gives equal hashes; carve shapes; energy accounting; a cut through the frigate's midsection separates the bow; a cell order that does not depend on container iteration |
| 3.2 | Materials (strength, mass, yield) beside the emissive table; palette roles extended with an **ore** family now, so every model is regenerated once (ADR-002's consequence) | `GameCore/Palette.*`, `AssetGenerator/`, all `Assets/Models/*.vox` | Palette test across every regenerated file; `AssetGenerator --check` |
| 3.3 | Module regions: named boxes in VOX coordinates with a function and two thresholds (reduced, stopped), one of them the core. Emitted by the asset generator next to each model so that geometry and regions cannot drift | `AssetGenerator/`, `Assets/Content/Modules.txt`, `GameCore/ModuleRegion.*` | Regions inside their model; exactly one core per ship; the frigate's authored damage reproduced from a fixture mask |
| 3.4 | Weapons: launcher records, the firing cycle with chapter 03's refusals in order, power per shot, bolts as traced segments with muzzle speed and lifetime, the lead point | `GameCore/Launcher.*`, `GameLogic/Combat.*` | Each refusal in order; the clock restarts on an attempt that fails for power; the lead point against a crossing target |
| 3.5 | Shields: capacity, regeneration drawing on the power pool, collapse at the threshold, the rebuild time, energy damage draining the pool, no bleed-through | `GameCore/Shield.*` | Chapter 03's state table; the worked example's shot count to collapse with its numbers |
| 3.6 | Hits on the authority: bolt against shield bubble, then against the target's voxels by DDA through the mask; a hit becomes a damage event; module health becomes the share of its cells present; functions degrade and stop at the thresholds (no engine cells, no thrust) | `GameLogic/Combat.*`, `NeuronCore/RayBox.*` | A bolt through a gap in a damaged hull passes through; an engine region carved away stops thrust |
| 3.7 | Death and D1: core stopped, breakup into wrecks, the player returned to the outpost with the base snapshot fit and no cargo; D2: an indestructible flag that the authority honours | `GameLogic/World.*`, `GameLogic/Character.*` | Scenario: a ship killed by carving its core; the player's return; the command block survives any event |
| 3.8 | The opponent: chapter 05's manoeuvre layer with attack pass, evade, trail and flee; a target chosen by an owner tag until factions exist; fire control with burst and accuracy from a pilot record; aim at a chosen module | `GameCore/Pilot.*`, `GameLogic/Ai.*`, `Assets/Content/Pilots.txt` | Behaviour vetoes; the pass rhythm (run in, pass, extend); accuracy cone against a player per pilot tier |
| 3.9 | The client: applies damage events through the same function, updates surfaces per ADR-013, plays hit sparks, breach embers and debris, draws wrecks; hostile models (a raider hull recipe in the generator) | `GameApp/`, `NeuronClient/Renderer.*`, `AssetGenerator/ShipModels.cpp` | Client and server masks hash equal after a scripted fight |
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
| 4.2 | New module models by recipe: reactor, habitat, refinery, turret platform, shield generator, yard | `AssetGenerator/StationModels.cpp`, `Assets/Models/` | `--check`; palette; size limits |
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
| 5.1 | The claim: `AsteroidMajor` as an authored solar whose mask is saved (run-length encoded; it is 2.6 million cells), with ore-family voxels seeded by the generator in seams | `AssetGenerator/AsteroidModels.cpp`, `GameLogic/Character.*` | Save round trip of a mined claim; encoded size recorded |
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
| 6.4 | Wing commands in the flight view (form up, attack my target, defend, dock, hold fire) and orders in the tactical view (multiple selection, go to, attack, defend, dock, join formation, assign itinerary), all issued as directives | `GameApp/FlightView.*`, `GameApp/TacticalClient.*`, `GameLogic/World.*` | Each order becomes the right directive; box selection |
| 6.5 | Raids: an encounter record, an "assault this solar" itinerary, spawn around the present player from the claim's zone, a battle cap and a relief time (chapter 06) | `Assets/Content/Encounters.txt`, `GameLogic/Population.*` | Raid size from the zone; no second wave inside the relief time |
| 6.6 | Outpost defence: turret platforms and the shield generator as equipment on sockets, flown by a pilot record; a garrison on a defend directive | `GameLogic/Combat.*`, `GameLogic/Ai.*` | Turrets fire at hostiles only; a stripped turret stops |
| 6.7 | The scenario: the three objectives as an authored opening, using the event bus and objective evaluator: a raid to defend against, F-03 to build, T-041 arriving with an ambush on its way to dock; failure when the tug dies | `Assets/Content/Opening.txt`, `GameLogic/Objectives.*` | Scenario: each objective completed; the tug's loss fails its objective; the opening survives save and reload at any point |
| 6.8 | The fly-or-command dial: pilot accuracy against players and the own ship's handling under a directive, tuned until the player's own flying is measurably better than the stand-in's in the duel of slice 3 | `Assets/Content/Pilots.txt` | The headless duel run with the player's ship under a directive loses more often than the scripted player input |

**It works when** "defend the shipyard" can be won from the cockpit and from the tactical view, "escort tug T-041 to dock" can be lost, and all three objectives complete in one session that survives a save and reload.

**The checkpoint.** At the end of slice 6, play the MVP for an hour and answer the design's §12 honestly: is the scene worth playing, do the two views both earn their place, does the outpost pull the player back too strongly? The answer decides whether slices 7 to 10 start as designed, start changed, or do not start.

## 11. What the MVP stubs

| Missing system | Stand-in in the MVP | Replaced in |
|---|---|---|
| Factions and regard | An owner tag: the player's, hostile, neutral | Slice 7 |
| Population | Raid encounters only; the tug and miners are owned | Slice 7 |
| Markets, money, worth, level | The player starts with plans and Alloy; money exists in the record and nothing spends it | Slice 8 |
| Other systems, lanes, jumps, map | One system | Slice 8 |
| Trigger language, campaign, job board | The event bus with a minimal objective evaluator and one authored opening | Slice 9 |
| Network transport | The in-process transport over encoded messages | Slice 10 |

## 12. Decisions to write as ADRs

| ADR | Subject | Due before | The question it answers |
|---|---|---|---|
| 007 | The simulation tick and time | F2 | The tick rate, how render frames relate to ticks, interpolation |
| 008 | Commands, state and the in-process transport | F3 | What travels up and down, encoding, ordering, who pumps |
| 009 | Content records and identifiers | F4 | Which record files, typed fields, reference checking, socket classes, how `Scene.txt` divides |
| 010 | Moving instances and the shadow map | Slice 1 | Per-frame transforms, shadow camera and redraw policy, with the F6 measurement |
| 011 | The character record and save format | Slice 2 | Sections, content's save interface, atomic replacement, mask encoding |
| 012 | The voxel mask and the damage event | First carve (slice 3) | The event's fields, integer application, cell order, separation (R22) |
| 013 | Changing surfaces in the renderer | Slice 3 | Whole-instance or brick re-extraction, buffers per instance, upload path, the measurement behind the choice |
| 014 | Materials and module regions | Slice 3 | Material fields, the ore family, regions emitted by the generator |
| 015 | Modules, construction and outpost rates | Slice 4 | Module records, build order of cells, integer rates and shortage priority |
| 016 | The event bus and objectives | Slice 4 | Event types, priority order, re-entrancy, the objective evaluator's limits |

## 13. Order and dependencies

```text
F2 tick ── F3 messages ── F4 server owns the world ──┐
F5 scripted smoke test ──────────────────────────────┼── slice 1 ── slice 2 ── slice 3 ── slice 4 ── slice 5 ── slice 6
F6 measurements ── (decide ADR-010, ADR-013) ────────┘
F1 checkers: alongside everything
```

Slice 3 needs slice 2's save (task 2.6) for D1's return to base. Its palette change (task 3.2) regenerates every model, so it lands before any new model of slices 4 and 5. Inside a slice, the server-side rules and their tests come first, then the client. F1 can run alongside everything else; F6 must finish before ADR-010 and ADR-013 are written, because both are decided by its numbers.

## 14. Basis

The code as of commit `d331bcf`: `Outpost/Main.cpp`, `Outpost/MainWindow.h`, `NeuronServer/TickClock.h`, `GameLogic/World.*`, `GameCore/WorldSnapshot.h`, `GameCore/SceneDefinition.h`, `GameCore/Palette.h`, `NeuronCore/VoxModel.h`, `NeuronCore/VoxelMesher.h`, `NeuronCore/RecordFile.h`, `NeuronCore/ModelAxes.h`, `NeuronClient/Renderer.h`, `NeuronClient/GpuTypes.h`, `NeuronClient/OrbitCamera.h`, `GameApp/TacticalClient.*`, `GameApp/TacticalScene.h`, `GameApp/SceneAssets.h`, `.github/workflows/build.yml`, and the model sizes in the `.vox` files. The design: [GameDesign.md](GameDesign.md). The Frontier blueprint's chapters 01 to 06, 09 and 11, for every rule and number cited.

Nothing here was built or measured. Every figure in this plan is the blueprint's or arithmetic on file sizes, and the choices marked "recommendation" are for the ADRs to confirm or overturn with measurements.
