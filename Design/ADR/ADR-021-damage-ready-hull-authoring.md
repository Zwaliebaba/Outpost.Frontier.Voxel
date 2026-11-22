# ADR-021 — Damage-ready hull sources and authoring fixtures

**Status:** accepted for asset authoring · **Date:** 2026-11-22

**Asset refresh:** ADR-022 adds signal/glazing materials and inspection recesses; measured counts
and hashes below refer to the refreshed sources.

## Context

The owner requested damage-ready hull assets. The frigate's existing VOX contains a permanently
carved starboard breach, scorch colors and glowing fragments. A future presence mask alone cannot
repair cells absent from its archetype, or distinguish original material from cosmetic burn colors.
The MVP requires an intact archetype, named functional regions and reproducible initial damage.

The repository now has ADR-009's record conventions. The runtime mask/event protocol, module
health thresholds and material strengths are still the subjects of ADR-012 and ADR-014. This art
pass must provide usable source data without implementing or preempting those systems. ADR-010
through ADR-020 are already written or reserved by the implementation plan.

## Decision

- Add one editable, canonical-palette `*Intact.vox` master for each current vessel: Frigate,
  EscortLancer, EscortTalon, Tug and Raider. Preserve their original grids, axes, pivots and scale.
- Restore the frigate's missing starboard geometry from its undamaged port counterpart, following
  the original authoring's mirror construction. Restore short ties to the three isolated residual
  fragments outside that mirrored envelope. Every master is connected to its designated core.
- Color enclosed structure, decks, conduits and functional components for readable inspection and
  subsequent damage. These masters contain no scorch or breach-ember voxel indices. Healthy lamps,
  engine glow and internal core illumination remain permitted.
- Keep the existing scene VOX files unchanged as compatibility previews. Runtime placement and
  existing ship archetype records continue to use them until the damage system is integrated.
- Store **authoring-only** metadata in `Assets/Hulls/`, using ADR-009's line-record grammar and
  stable ASCII identifiers. This is not a new runtime `Assets/Content/` loader.
- `Hulls.txt` declares versioned hull references, dimensions, master SHA-256 hashes and named
  half-open VOX boxes with function tags. It has exactly one core per hull and no health thresholds,
  prices, strengths, flight parameters or loadout rules. Structural diagnostic boxes may cover
  functional regions; these are annotations rather than exclusive voxel-ownership rules.
- `FrigateF317.txt` declares a versioned initial damage preset. `remove start=N count=M` spans
  are sorted, non-overlapping ranges of **solid master cells**. The authoring index is
  `x + sizeX * (y + sizeY * z)`. Spans are integer-only, within bounds and bound to the master hash.
- Keep **presence** and **appearance** separate. A hash-bound `appearance model=Frigate` reference
  supplies the exact legacy palette appearance. The tool expands only its differences into a
  cosmetic overlay on still-present cells. It does not change simulation materials or presence.
  Referencing the existing preview avoids duplicating tens of thousands of palette overrides.
- Repairing the authoring state restores all master cells and clears cosmetic overrides. It must
  reproduce the exact serialized clean master; applying the initial preset must reproduce the
  exact serialized legacy frigate.
- These record spans are an **asset fixture convention**, not the bit layout, event encoding,
  replication format or repair algorithm of the game. ADR-012/014 will define runtime import and
  behavior. In particular, do not use the baked preview's cosmetic colors as simulation materials.
- `PrepareHulls.py` is an optional development authoring tool, not a build step. Committed VOX
  files remain source assets. Its `--refresh` preserves edited masters and refreshes their hashes
  and the fixture; a separate output directory supports reproducibility checks.
- Ship the new models and sidecars via normal Debug/Release asset-copy items. Validate source
  integrity and reconstruction in CI using development-only NumPy; no runtime dependency is added.

## Evidence

Measured by `Tools/ValidateHullAssets.py` over the committed sources: five connected intact masters,
43 nonempty in-bounds regions, 5,063 absent frigate cells and exact damaged/repair reconstruction.
A three-cell-wide full-grid cut at VOX Y=70..72 detaches 38,109 cells from the frigate's core.
Counts and hashes are recorded in `Design/HullReview/HullValidation.json`; previews are offline
Blender renders of these assets, not evidence of a live gameplay damage implementation.

## Consequences

The next damage slice can import an intact model and a preset rather than inventing geometry
from a damaged screenshot. Artists can inspect real internal components with cutaway previews.
Any source edit invalidates the old fixture hash until deliberately refreshed and verified.

The existing sample and current `Ships.txt` remain compatible. The fixture preserves the old
frigate's loose breach fragments; runtime connectivity/separation can turn them into wrecks when
that system arrives. The authoring pass neither implements combat nor assigns degradation
thresholds, and its occupancy counts do not replace the sample's manually authored HUD percentages.
