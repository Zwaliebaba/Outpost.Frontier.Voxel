# ADR-014 — Materials and module regions

**Status:** accepted · **Date:** 2026-11-23 · **Due by:** plan slice 3 (tasks 3.2 and 3.3)

## Context

GameDesign.md §7.1 makes a ship its voxels: each palette family is a material with a strength, a mass and a yield; a module is a named region of a model's voxels with a function, whose health is the share of its voxels present; below one threshold it works at a reduced rate and below a second it stops; one module is the core, whose loss destroys the ship. ADR-021 prepared intact masters, named regions and the F-317 preset as authoring data, and left their runtime import, thresholds and materials to this ADR.

## Decision

### Materials sit beside the palette

`GameCore/Palette` gives each colour index a `StructuralMaterial`: a strength in carve units, a mass in kilograms per cubic metre at one voxel a metre, and an Ore yield.

| Family | Strength | Why |
|---|---|---|
| Charcoal hull plating, safety orange, hazard stripes | 20 | the base |
| Grey and off-white armour | 40 | armour is a stronger outer material, not a divisor (§7.1) |
| Steel frames, rock, ore | 30 | between |
| Dark frames, teal insignia (core and shield hardware) | 15 | fittings |
| Decks, scorch, embers, lamps and lights | 10 | goes easily |
| Glass and blue glazing | 8 | goes most easily |

Ore yields 1 a cell; nothing else yields (slice 5 reads it). Mass is recorded and not read: mass does not follow the mask in the MVP (ADR-012). Every index any model uses has a strength above zero and below `UNBREAKABLE`, which a test checks across every shipped model.

**A player's module cells cost three times as much** (`PLAYER_MODULE_TOUGHNESS`). GameDesign.md §7.1 keeps the blueprint's protection of a player's parts and equipment (ch. 03 §3.4: three and five times tougher than an NPC's in the same hull) as a material factor. It applies to module cells only, as the blueprint applies it to parts and not to the hull. The authority sets it on the event (`toughened`), so both halves apply the same factor.

### Hulls.txt is loaded at run time

- `Assets/Hulls/Hulls.txt` stays where ADR-021 put it, in its grammar, and is now runtime content: `GameCore/ModuleRegion`'s `LoadHullContent` reads it on both halves. Every region must lie inside its hull's grid, and every hull has exactly one core region.
- **A hull's master is `Assets/Models/<model>.vox`**, named by the hull record's `model=`. Its `legacy=` scene model is what places and draws the ship, and must have the master's grid.
- **Every other `.txt` in `Assets/Hulls` is a damage preset**, named for its file. Its `remove` spans are sorted, apart and inside the grid, and its `appearance` record is presentation, which the mask and the server never read. An instance takes one with `preset=` in `Scene.txt`, and it must be a preset of that instance's hull.
- **The SHA-256 fields are not checked at run time.** `Tools/ValidateHullAssets.py` checks them in CI's Linux job, and a runtime SHA-256 would be code that only repeats that check.

### Thresholds are per function, in Modules.txt

`Assets/Content/Modules.txt` holds one `function` record per function: `reduced` and `stopped` in whole percent, and the `reducedrate` a weakened module works at. A function that only stops gives both the same value. A function with no record never weakens, and a module with no cells left has always stopped.

| Function | Weakens below | Stops below | Weakened rate | What its rate scales |
|---|---|---|---|---|
| core | — | 50 % | — | its loss destroys the ship |
| power | 60 % | 20 % | 50 % | the power pool's charge |
| shield | 60 % | 20 % | 50 % | the shield's regeneration |
| engine | 60 % | 20 % | 50 % | thrust: the mean of the engine modules' rates |
| weapon | — | 50 % | — | the launcher mounted in it is disabled |
| sensor | 60 % | 20 % | 50 % | nothing yet: slice 3's contacts |

- **Health is a whole percent, rounded down**, so both halves read the same number from the same mask.
- **The condition is read from the stated health.** `ConditionOf` turns the modules' health into a ship's condition: thrust, charge and regeneration rates, each mount working or not, and the core. The server applies it to the archetype's spec with `ApplyCondition`; the client applies the same function to the health the server states for its own ship, before it predicts the next tick. So a damaged ship's prediction matches the server's without the mask being on the client.
- **"Hull" is the share of structure cells present** (§7.1): the master's cells outside every functional region. A structure region (the frigate's `HullSectionB`, the Talon's wing struts) counts as structure and is listed among the modules.
- **Weapons are mounted in weapon modules.** A `mount` record in `Ships.txt` puts a launcher in a weapon region of the hull, and its muzzle is the middle of the region's forward face.

### The calibration, and how it was set

The calibration target is the blueprint's rhythm (§7.1): seconds to break a shield, a few more seconds of exposure, a decision inside the window, and time to core in place of time to zero hull. The first tier's numbers are the blueprint's where it has them, and the carve was set by a headless duel:

- **The gun:** 28 damage for 8.5 power, five shots a second, 600 m/s for a second, the blueprint's class 1 rates (ch. 03 §4: median 136 damage and 42 power a second).
- **The shield:** 450, the blueprint's smallest, collapsing at 15 % and rebuilding for 12 s, regenerating 10 a second for 10 power, so that it refills from nothing in 45 s (ch. 03 §4).
- **The carve:** a capsule of radius 2, six centres long along the shot, with 400 units of energy. Four settings were tried in a three-minute duel, with the earlier shield of 600, in which the raider got onto the scripted player's tail. With a radius-2 sphere at 90 and with a radius-1 capsule at 200, the player's ship survived the three minutes, at 83 % and 61 % hull. With a radius-1 capsule at 300 it was destroyed after about two and a half minutes, and at 400 after under two. The shield was then lowered to 450, and the duels below were run with that.

**What the duel measured**, on Linux with the shipped content (`CombatTests`, and the same scenario in a scratch harness):

- **The raider against a scripted player**, 1.5 km apart, the player flown by the raider's own pilot record: the head-on pass hit both shields at 6.2 s. Manoeuvring broke the raider's shield 23.1 s after it was first hit. Its core was reached 1.5 s after that, and it fled at once, below its pilot's 80 % core threshold, and was gone 102.8 s into the run.
- **The raider against a player that does nothing:** the shield broke 3.3 s after the first hit. The core of the toughened player was stopped after 95 s of fire, about seven windows. The player was then back in its berth at the shipyard, as fitted there.

**This is not tuned by hand, and it does not meet the target for a player's ship.** A sitting target's shield breaks in seconds, as the blueprint wants. A player's core takes about seven windows against a raider's two or three, because a player's modules are three times tougher. Whether that is right is a by-hand question for the owner, with the frame budget, which these numbers do not touch.

## Consequences

- **Content holds every number.** Strengths are in code beside the palette, as the emissive table is; thresholds, weapons, shields and mounts are records. A rebalance is a content change, and every recorded duel number moves with it.
- **A new hull needs a master, regions with one core, and its legacy model.** A ship model with no hull is still carved against its own voxels, but has no modules, no core and cannot be destroyed.
- **Mass does not follow the mask yet.** A ship that loses a wing flies as it did, until a later ADR gives flight its mass from the cells present.

## What this forecloses

- **Per-cell material overrides.** A cell's material is its colour's. A different material means a different colour index, and the palette is shared (ADR-002).
- **Modules that overlap with exclusive ownership.** Regions are boxes and may overlap; a cell in two regions counts for both. A module made of an arbitrary set of cells would need another record.
