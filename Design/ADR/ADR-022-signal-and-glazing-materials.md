# ADR-022 — Yellow strobes and blue glazing

**Status:** accepted · **Date:** 2026-11-22

## Context

The owner requested a bright yellow palette color for strobes, a see-through blue for windows,
and fittings on the editable models in `Assets/Models`. Slots 224 and 225 were unused by every
model. The existing renderer forced alpha to one, treated every occupied neighbor as opaque,
and emitted steady light. Simply changing RGB would provide neither transparency nor flashing.

## Decision

- Reserve voxel index **224**, `STROBE_YELLOW`, RGBA **255,224,48,255**. Its canonical linear
  emission is **10,8.2,1.1**, with a 3.2-voxel halo and a **1.5-second** presentation period.
  Two flashes occupy phases `[0,.06)` and `[.15,.21)`; the remainder emits at 2.5% brightness.
  Voxel radiance and additive halos use the same shader function and presentation time.
- Reserve index **225**, `WINDOW_BLUE`, RGBA **75,162,235,80**. All other addressed palette
  entries retain their colors and opaque alpha. Existing dark glass and amber window roles remain.
- Use stable **4×4 screen-door coverage** for runtime glazing: 5 of 16 samples at alpha 80/255.
  This keeps the existing depth-writing voxel path and avoids sorting individual windows, ships
  and station modules. It is an explicit approximation: no refraction, continuous tinted blending
  or accumulation through stacked glass. Every pane uses the same screen pattern.
- Extract the render surface with the canonical palette. Transparent neighbors expose the opaque
  backing faces; equal glass neighbors still hide their shared face. Glass stays solid for core
  connectivity, damage occupancy and other gameplay. It does not cast the key-light shadow.
- Embed two standard MagicaVoxel **MATL** dictionaries in every source file: yellow `_emit`
  with `_weight=1`, `_emit=1`, `_flux=3`, `_rough=.2`; blue `_glass` with transmission/weight
  `.68627451`, roughness `.05`, IOR offset `.3`, and attenuation `0`. These are editor attributes;
  the game retains ADR-002's canonical material authority. MagicaVoxel emission is static;
  flashing is a runtime presentation behavior rather than a VOX animation.
- Extend the native VOX reader/writer to preserve ordered MATL dictionaries when supplied to
  `WriteVox` or `SaveVoxFile`. Reject truncated/duplicate dictionaries and material identifiers
  outside 1..255. Unknown property names are preserved without interpreting them as game rules.
- Add distributed top-facing strobes to manufactured models and shallow recesses behind selected
  existing cockpit/observation panes. Preserve grid sizes, pivots, sockets, the core regions and
  connectivity of intact hulls. Retain supports around thin canopies. Natural rock and loose debris
  receive the shared palette and editor attributes without artificial fittings.
- Refresh the hash-bound hull catalog and initial damage fixture. Presence and appearance remain
  separate (ADR-021), with exact starting-preview and clean-repair reconstruction.

## Verification and limits

`Tools/ValidateSignalMaterials.py` measures **32** identical palettes and authoring material sets,
**25** models with yellow strobes and **17** with blue glazing. Hull validation still finds five
connected cores and the same **5,063** absent F-317 cells. The native unit tests cover material
round trips and malformed dictionaries, palette alpha/emission roles, and backing faces behind
glass. CI renders the actual Windows executable on WARP at both flash phases and between them,
and uploads the captures/reports for review. Offline Blender previews show the actual VOX geometry
using glass transmission; they are not screenshots of runtime screen-door coverage.

No texture format changes, flight balance, physical transmission or damage rules are introduced.
DDS remains the intended format for future gameplay UI textures.
