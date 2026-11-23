# ADR-012 — The voxel mask and the damage event

**Status:** accepted · **Date:** 2026-11-23 · **Due by:** AGENTS.md R22, before the first code that carves

## Context

Slice 3 makes voxels go away. AGENTS.md R22 makes the voxel mask the game's deterministic core: client and server must arrive at the same voxels from the same event, the code that applies an event is one function in one library used by both halves, and a client that joins late is brought up to date by a snapshot or a diff of dirty bricks that names the last event it includes. The plan (section 7) sketched the event and the order of cells; this ADR fixes them.

The owner decided four things for slice 3 on 2026-11-23 (GameDesign.md D22 to D25), one of which binds here: damageable ships are taken against the intact masters of ADR-021, and the frigate starts with the F-317 preset as its mask (D25).

## Decision

### The mask

- **One bit per cell of the archetype's grid**, present or absent: `Neuron::VoxelMask` in `NeuronCore/VoxelMask.*`. Colour and material stay with the archetype, so a repair restores exactly what was there.
- **A cell's index is `x + sizeX * (y + sizeY * z)`**, the index ADR-021's hull fixtures already use, so a preset's spans are mask spans.
- **The archetype** of an object is its hull's intact master where `Assets/Hulls/Hulls.txt` has one for its scene model, and the scene model itself otherwise (ADR-014). Its initial mask is the archetype less its instance's `preset=`.
- **The hash** is FNV-1a over the grid's size and the 64-bit words of the mask. It is what "the two masks are equal" is tested by; the last event's number is not part of it.
- **Bricks are 16 cells on an edge**, 4,096 bits. F6 measured surface extraction at 0.07 to 0.10 ms a 16³ brick on one core (F6-RendererBaseline.md); ADR-013 decides the renderer's use of them. The mask itself is stored whole, not by brick: a brick is a view of it for snapshots, diffs and, later, the renderer.

### The events

A mask changes only by one of two events, applied by `ApplyMaskEvent` in `GameCore/MaskEvent.*` on top of `Neuron::Carve`, `Neuron::Patch` and `Neuron::DetachedParts` in `NeuronCore/VoxelCarve.*`. The authority decides the event; every input to its application is integer content both halves load, the event, and the mask.

**A carve** (damage) carries:

| Field | Type | What it is |
|---|---|---|
| `target` | object id | the object whose mask it changes |
| `sequence` | 64-bit | the event's number on that mask, from 1; the mask records the last one |
| `cell` | 3 integers | the impact cell, in the archetype's VOX coordinates |
| `direction` | 3 integers | the shot's direction in VOX axes, scaled so its largest component is about 64 |
| `shape` | sphere or capsule | a capsule is spheres along the direction |
| `radiusCells`, `lengthCells` | 8-bit | the sphere's radius; the capsule's number of sphere centres |
| `energy` | 32-bit | what removing cells may cost, in material strength units (ADR-014) |
| `seed` | 32-bit | the event's own, from the tick, the shooter and the mount |
| `toughened` | bool | the target is a player's: its module cells cost three times as much (ADR-014) |
| `cause`, `inflictor` | enum, object id | weapon, collision, hazard or repair, and who; presentation and listeners read them, the arithmetic does not |

**Applying a carve**, integers only:

1. **The candidates** are the present cells within the radius of each sphere centre: the impact cell, and for a capsule the cells a three-dimensional Bresenham walk visits along the direction, `lengthCells` in all.
2. **They are taken in one fixed order:** the first sphere centre that holds them, then the squared distance to it, then the cell index. Nothing depends on a container's iteration order.
3. **A candidate goes** when it is present, is exposed (it is the impact cell, or a face neighbour is absent or outside the grid, counting cells this event already removed), and the energy left covers its cost. The cost is its material's strength plus `hash(seed, index) mod (strength / 2 + 1)`, which roughens the edge. A cell not yet exposed when its turn comes stays, so energy does not tunnel behind an unbroken layer; armour holds as armour.
4. **Separation:** on an archetype with a core, the present cells no longer connected face to face to a present core cell are found, by a fill from the core over the whole mask, and taken out of the mask as part of the event. They become wrecks. If no core cell is left, nothing is separated by the event: the ship's death, which the authority decides from the core's health, breaks it up.

**A patch** (repair) carries `sequence` and `maxCells`. Applying it restores, in ascending index order, each cell that is absent, is a structure cell of the archetype, and touches a present face neighbour, counting cells this patch already restored, up to `maxCells`.

The authority's only float work on a mask is turning a hit into a cell: the bolt's segment is taken into the archetype's VOX space through the object's pose, scale, pivot and voxel size, and walked cell by cell (Amanatides and Woo) to the first present cell (`GameLogic/Combat`'s `TraceMask`). The event then names that cell in integers, and both halves apply it from there.

### The separation function's contract

`Neuron::DetachedParts(mask, anchors, removed)` returns the present cells no longer connected to any present anchor, grouped into connected parts, each part's cells ascending and the parts ordered by their first cell. It takes the cells the last event removed so that a later version can search only near them (GameDesign.md §8.2: a local fill where a removal may have cut a bridge, over a graph of bricks). In the MVP it fills from the anchors over the whole mask. Its callers do not change when it does.

### Snapshots and diffs name their last event

- `Neuron::MaskSnapshot` is the whole mask and the last event it includes.
- `Neuron::MaskDiff` is the bricks in which two masks of one archetype differ, each as 4,096 bits, and the last event the newer includes.
- A client that takes either, then applies the events after it, holds the server's mask. `VoxelMaskTests` shows both: six events missed, caught up, three more applied, hashes equal.

### What the catalogue carries now

Catalogue version 5 (ADR-008):

- **`FlightInput`** gains the fire switch and the aim point (three doubles). Firing is part of the shared ship tick, so the client's prediction pays for its own shots exactly as the server does.
- **`OwnShip`** gains the shield, whether it is up, its rebuild ticks, and each launcher's clock.
- **`ObjectRemoved`**, reliable on the universe stream: an object that was destroyed or fled. Its ship state and motion go with it.
- **`UseItem`**, reliable on the commands stream: a repair kit.

**The damage event itself is not on the wire yet.** The client cannot draw a mask until slice 3's client work (plan tasks 3.9 and 3.10, with ADR-013). Until then the server keeps a log of every event it issued (`Combat::Events`, at most 65,536), the client is told the measured health of each ship's modules, and wrecks are not published. The event's wire form is the table above, and it lands with task 3.9: reliable, ordered per object, on the universe stream (ADR-008's delivery class for damage).

## Evidence

- `VoxelCarveTests` (NeuronCoreTests): the same event on two masks gives the same cells in the same order and equal hashes; energy is never overspent; a capsule bores along the shot; an unbreakable layer stops the carve behind it; a patch restores only allowed cells that touch the structure.
- **The frigate's midsection:** a three-cell cut across `FrigateIntact.vox` at VOX Y = 70 to 72 detaches **38,109** cells from its core, the figure ADR-021 measured with its own tool.
- `MaskEventTests` (GameCoreTests): 60 carves, toughened and not, applied to two masks of the Lancer's master give equal hashes after each.
- `CombatTests.TheScriptedDuelKeepsTheBlueprintsRhythm` (GameLogicTests): every event the server issued in a duel, replayed through `ApplyMaskEvent` onto fresh initial masks as a client would, gives masks that hash equal to the server's.

All of these were run on Linux with g++ 13 against a stand-in for the test framework; CI runs them on Windows with MSVC.

## Consequences

- **Floats never decide a cell's fate.** A material strength, an energy, a radius and a cell order are integers, and the seed is the event's. Changing any of them changes every recorded result, so they are content, and changing one is a deliberate act.
- **A full fill per carve.** At the MVP's rate of events, a fill over the Lancer's 26,208 cells or the frigate's 288,000 costs well under the frame. At §8.2's battle it would not; the contract above is what lets it be replaced.
- **The log is a stand-in.** Until task 3.9 puts events on the wire, what proves R22 is the replay test, not a client.

## What this forecloses

- **Partial voxels.** A cell is there or not. Damage as a share of a cell, or a cell's colour changing with damage, would be presentation, never part of the mask.
- **Order-dependent shapes.** No shape may depend on which cells an earlier, unordered pass happened to visit; every shape is a list of centres and a radius, ordered as above.
- **Physics on the mask.** Mass and inertia do not follow the mask in the MVP; a ship that loses half its cells flies as it did, its modules aside.
