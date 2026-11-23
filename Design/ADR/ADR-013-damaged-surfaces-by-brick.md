# ADR-013 — Damaged surfaces, by brick

**Status:** accepted on F6's figures; the GPU frame budget while carving is not yet measured · **Date:** 2026-11-23 · **Due by:** plan slice 3 (task 3.9)

## Context

ADR-003 draws a model as the GPU buffer of its surface voxels, extracted once at load and shared by every instance of it. Slice 3 makes masks change (ADR-012): a hit carves cells, a part comes loose as a wreck, a repair puts cells back. The client must draw each damaged object from its own mask, and do it without a frame that stalls on re-extracting a whole frigate per hit, which at GameDesign.md §8.2's scale would be hundreds of extractions a second.

The plan (section 7, "Surfaces") made damaged models bricked from the start and left the brick size and the per-frame budget to F6's measurement. F6 measured surface extraction at 0.07 to 0.10 ms for a 16³ brick on one core of the reference machine, Release (F6-RendererBaseline.md). The owner accepted this ADR on those figures on 2026-11-23, with the GPU side to be confirmed by a `--benchmark` run on the reference machine.

## Decision

### A brick is the unit of re-extraction

- **Bricks are 16 cells on an edge**, the mask's own bricks (`Neuron::MASK_BRICK_CELLS`, ADR-012).
- **A brick's surface is extracted with every neighbour read from the whole model** (`Neuron::ExtractSurfaceRegion`), so the bricks that tile a model give exactly `ExtractSurface`'s voxels: the same faces, the same occlusion, no seam at a brick's edge. `VoxelMesherTests.BricksTogetherGiveTheWholeSurface` checks it on a ragged block cut into 8-cell bricks.
- **A changed cell marks every brick within one cell of it.** A cell is read by its six neighbours' faces and by the occlusion of the cells around it, so those are the bricks whose surface it can change, and only those are extracted again.

### At most twenty bricks a frame, nearest first

- **The budget is 20 bricks a frame** (`MaskedSurfaces::BRICKS_PER_FRAME`): at F6's 0.07 to 0.10 ms a brick, about 2 ms of one core. A brick that has no surface before or after costs nothing against it.
- **The nearest objects to the camera go first**, then the rest. The plan asked for "nearest and largest on screen"; the distance stands in for both until something on screen is large and far.
- **An object's surface is swapped whole, once no brick of it waits.** A frame never draws half an event. A mask is always current; only its drawing lags, by a frame for a hit and by a few for a first build (below).

### A damaged object draws its own surface; an undamaged one shares

- **The client holds a mask only for an object that something has changed** (`GameApp/ClientCombat`): the first event or snapshot for it creates the mask from its instance's initial mask (its `preset=`, D25), or from nothing for an object the scene did not place: a wreck. Every other object keeps its shared model and GPU buffer.
- **Its appearance is its archetype's colours, with its scene model's colours where the scene model has a cell of the same grid** (`MaskedSurfaces::BaseAppearance`), less the cells its mask lacks. That is D25's scorch overlay. Undamaged, the frigate, the Lancer, the Talon and the raider each extract to exactly their shared models' surfaces, so the swap to their own surface does not show.
- **Its surface is uploaded whole** as a new GPU buffer when it is rebuilt, before the frame begins (`Renderer::UploadModel`, which waits for its copy). The buffer it replaces is released three frames later (`GraphicsDevice::BACK_BUFFER_COUNT` and one more), when no frame that read it can still be in flight. A buffer per brick, sub-allocated, would upload less per hit; it waits for a measurement that says the whole upload costs too much.
- **A wreck is drawn from its own mask only**, with its parent's model's placement, and not at all until its first surface is ready.

### What the wire carries for it

Catalogue version 6 (ADR-008) puts ADR-012's event on the wire, as ADR-012 said task 3.9 would: `DamageEvent` and `MaskSnapshot`, reliable on the universe stream; and, for the tracers and sparks, `BoltFired` and `BoltHit`. Wrecks are universe objects (ADR-012's amendment).

## Evidence

Measured on Linux in the scratch harness, built by g++ 13 at `-O1` against stand-ins for the test framework and the D3D12 headers. **These are not Release figures on the reference machine**, and the per-brick time is two to three times F6's.

- **A 130-second duel, the player passive** (ADR-014's second duel), the server's messages through the observer into `ClientCombat` and `MaskedSurfaces` each tick: 95 frames re-extracted something, 203 bricks in all, at most 14 in one frame; 0.31 ms a frame on average and 1.98 ms at worst. Every one of the five masked objects, four of them wrecks, drew a surface equal to a whole extraction of its mask, and every client mask hashed equal to the server's. No event arrived out of order.
- **The frigate's first event** built its whole surface: 56 of its 90 bricks have cells, 12.7 to 13.9 ms in all (0.23 to 0.25 ms a brick), which the budget spreads over three frames. **Its second** re-extracted 2 bricks in 0.3 to 0.4 ms. Its surface is 17,097 voxels, 137 KB to upload.
- `ClientCombatTests` (GameAppTests): events applied in order and once; a snapshot against nothing; the budget; an event in a brick's middle re-extracts that column of bricks and none beside it; one on a corner re-extracts the neighbours too, and the result equals a whole extraction.
- `CombatTests.AClientThatHearsTheObserverHoldsTheServersMasks` (GameLogicTests): a duel heard through the observer, applied as a client applies it, ends with every mask equal to the server's.

**Not measured:** the GPU frame time while the frigate is carved, and the stall of an upload, which waits for the GPU's queue. Both are the owner's `--benchmark` run on the reference machine, against F6's 1.6 ms sample frame.

## Consequences

- **A first hit on a large hull shows a few frames late.** The frigate's 56 bricks take three frames at 20 a frame; until then its shared model is drawn, whole.
- **Every rebuild uploads the whole surface and waits for the copy.** At a duel's rate of hits that is a few uploads a second. If the benchmark shows the stall, the next step is a buffer per brick, then a copy that does not wait.
- **A damaged object's sideways profile and wireframe stay its shared model's.** The selected-ship panel's silhouette does not show damage; its percentages do, measured by the server from the mask.

## What this forecloses

- **Partial voxels in the renderer**, as ADR-012 forecloses them in the mask: a cell is drawn or not.
- **Re-extraction outside the frame's budget.** No path extracts a whole damaged model in one frame; a snapshot marks every brick and waits its turn like a hit.
