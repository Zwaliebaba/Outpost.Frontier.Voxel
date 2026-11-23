# ADR-011 — The character record and the save format

**Status:** accepted · **Date:** 2026-11-22

## Context

Slice 2 saves the game for the first time (plan tasks 2.6 and 2.7). GameDesign.md decided when: on launch, on docking and on quit, into one slot, each save written completely before it replaces the last, with no manual saves (D17), and start-up resumes the save or starts a new game docked (D18). The blueprint gives the shape of what is saved (ch. 11 §3.10):

- **A character and a saved game are the same text record.** Its parts are a header, the location, the ship, a base snapshot, interface state, content's own parts, and in single player the world.
- **Content writes its own parts.** The authority raises "player save" and "world save", and each content controller appends its sections, so the authority stays ignorant of missions.

AGENTS.md adds three constraints that shape the code:

- **R23:** game state belongs to a character or to the world, never to the process, so a second player is a second record.
- **R24:** content is named by stable text identifiers, never by an index or load order, and that includes what a save records.
- **R20:** no wall time in game rules. The blueprint's header carries a time stamp, which the server half cannot read.

The plan asked for three properties to be tested: every field round-trips, a section the reader does not know is kept on rewrite, and a truncated file is refused with a reason. "It works when" adds that the save file is a readable text record.

## Decision

### The file is a record file in sections

A save is the record format of ADR-002, so a person can read it and edit it in a text editor:

```text
# A saved game of Outpost: Frontier, a text record (ADR-011)
save version=1
section name=Character
character money=0
location kind=docked station=Shipyard instance=Hangar point=Berth
ship instance=Lancer hull=100
baseship instance=Lancer hull=100
section name=Universe
universe tick=8412
resource name=Alloy amount=12400 remainder=37
objective text="Defend the shipyard" complete=1
end sections=2 records=7
```

- **`save version=1` comes first.** Any other version is refused with both numbers, rather than read as best it can.
- **`section name=` starts a section,** and every record up to the next one belongs to it. A section name may appear once.
- **`end` comes last and counts the sections and records before it.** A file cut short anywhere either lacks the end record or holds fewer records than it says. Both are refused as "incomplete", with the counts, and so is a file cut inside a quoted value, which no longer reads as records.
- **The format belongs to the engine.** `Neuron::SaveFile` in `NeuronServer/` formats and parses sections of records and knows nothing of what they mean (R9). `Neuron::FormatRecord` in `NeuronCore/RecordFile` writes one record, quoting a value that is empty or holds a space, a tab or a `#`. It refuses what the format cannot hold, a quote or a line break, rather than write a file that reads back differently.

### Content writes its own sections through an interface

- **`Neuron::SaveWriter` is all a part of the game is given:** `BeginSection`, `Add(keyword)` and `Field(key, value)`. It never sees a file or a handle.
- **Each part reads its own section back** from the parsed `SaveSection`, strictly. An unknown record or key, a missing key and a value that does not parse are refused, with the section and the line.
- **A section the game does not know is kept** as parsed and written back unchanged after the known ones. A save from a later version therefore survives being loaded and saved again by this one. Comments in a kept section are not kept; a save is written by the game, not by hand.
- **Slice 2 has two sections:**
  - `Character`, written by `WriteCharacter` (`GameLogic/Character`);
  - `Universe`, written by `WriteUniverse` (`GameLogic/SaveGame`).

### What the character record holds

| Record | Holds |
|---|---|
| `character` | Money |
| `location` | Docked: the station, and the instance and docking point of the berth. In space: the ship's whole flight state |
| `ship`, then `module` | The ship's instance, its hull, and each module's health |
| `cargo` | One record per item and count |
| `baseship`, then `basemodule` | The ship as it was when last docked |
| `interface` | Key and value pairs that belong to the client. The server keeps them and does not read them; nothing writes them yet |

- **Content is named by its text identifiers** (R24): the station from System.txt, the instance and the docking point from Scene.txt, module names, resource names, and objectives by their text, which is the only name they have.
- **Numbers are written in the shortest form that reads back to the same bits** (`std::to_chars`). A ship saved in space resumes with a flight state equal, bit for bit, to the one saved, which is what `FlightState`'s equality tests.
- **There is no character name.** The plan's record has none, and nothing in the game shows one.

### What the universe section holds

The universe's clock (the tick), each resource's amount and the ledger's remainder, and each objective's state. Everything else in the universe is content, and is built again from it on load.

### The file is replaced in one step

`Neuron::WriteSaveFile` writes the text to `<save>.partial`, flushes it to the disk, and moves it over the save with `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. A crash leaves either the previous save or the new one, never part of either. This is D17's "written completely before it replaces the last".

### Where the save is, and when it is written

Added with plan task 2.7, which implements D17 and D18.

- **One slot, in the player's Saved Games folder:** `Saved Games\Outpost Frontier\Save.txt`, found with `SHGetKnownFolderPath(FOLDERID_SavedGames)`, the folder Windows gives games for exactly this. `--save <file>` names another. A smoke test saves nowhere unless `--save` is given, so a verification run never touches the player's game and always starts the same way.
- **The universe asks; the executable writes.** `Universe::TakeSaveRequest` reports that the save rule fired: on launch, the tick the command is taken, with the ship saved docked in the berth it is leaving; and on docking, the tick the ship rests on the pad. The executable writes after that tick, and again on quit, wherever the ship is, so quitting in space keeps the sortie. The server half never sees a file (R18's split, and this ADR's interface rule).
- **A ship being carried is saved in its berth.** During the final approach, docked, and launching, the location is the docking point, so a save never holds a pose inside a scripted move that a resume could not continue.
- **Start-up (D18):** the save is resumed when it exists. Otherwise a new game starts with the player's ship docked at a free fitting point of `System.txt`'s start station, with a fresh character, and the first save is written by the first launch or the quit.
- **A save that cannot be read or does not fit the content stops the game, and is left as it is.** The reason and the path are shown, and nothing overwrites the file. A save that names an unknown station, point, resource or objective, or flies another ship than the scene gives the player, fails whole: `Universe::Restore` checks everything before it changes anything. Whether the player may then choose to start again is a question for the owner, not settled here.
- **What a resume reproduces, measured by test:** `LaunchFlyDockQuitAndRestartKeepsEveryField` launches, flies, docks, formats the save, parses it into a fresh universe loaded from the shipped content, and compares the save, the tick, the flight state and the ledger field by field, then runs both 600 ticks and compares the ledger again. `QuittingInSpaceKeepsTheSortie` does the same for a ship in space and compares the flight state to the bit before and after 120 more ticks.

### No time stamp

The blueprint's header has a time stamp, and the server half reads no wall time (R20). The file's modification time says when it was written; the universe's tick says how far the game had run.

### Amended for slice 3 (ADR-012)

Added with plan slice 3's server half, on 2026-11-23. Every new field is optional when read, so a save from slice 2 resumes.

- **A mask is kept as the cells it lacks against its archetype**, the intact master (ADR-012): `absent=start:count,...`, sorted runs of cell indices. The player's ship carries one on its `ship` record and on its `baseship` record; the universe section has a `mask instance=<name> absent=...` record for each object whose mask is no longer the one it started with. An unchanged frigate is not written, though its preset removes 5,063 cells.
- **`gone instance=<name>`**: an authored object that was destroyed or fled. It is not created again on resume (GameDesign.md D23).
- **The character** gains `repairkits=` on its `character` record and a `base station= instance= point=` record: where it was last docked, the base a death returns it to (D1).
- **A ship in space** gains `shield=`, `shieldup=`, `shieldrebuild=` and `launchers=` (four launcher clocks) on its `location` record. A save without them resumes with a full shield and ready guns.
- **Wrecks are not kept.** They are transient until slice 5 makes them salvage.

The voxel masks of slice 5's claim asteroid will use the same encoding; whether a 2.6-million-cell mask fits it as one record is for that slice to measure.

## What this forecloses

- **A binary save.** Anything a save holds has to be expressible as records of text values, quote-free. Masks are runs of absent cells (amended above).
- **Partial loading.** A save is read whole or refused, and a refused save stops the game rather than being replaced. There is no recovery of a damaged file; the previous one is gone once the replacement lands.
- **Hand-edited comments surviving.** A save is rewritten on every launch, docking and quit, and only records survive.
