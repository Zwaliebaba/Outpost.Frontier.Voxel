# ADR-009 — Content records and identifiers

**Status:** accepted · **Date:** 2026-11-22

## Context

Today every piece of content is in one file, `Assets/Scene.txt` (ADR-002). It holds placement, presentation and the gameplay state the server starts from, and only the client parses it: the server is handed the client's parsed copy (`world.Load(session->client.Scene())`). The MVP plan's next tasks need more than that.

- **F4** has the server own the world, so it must read its own content and give every object an identity.
- **Slice 1** adds ship archetypes: mass, drag, thrust and turn rates per hull.
- **Slices 2 to 6** add docking points, modules, weapons, shields, pilots, encounters and an authored opening.

Three AGENTS.md rules bear on this:

- **R24:** content is named by a stable text identifier, every reference is checked at load, and nothing persistent or on the wire identifies content by load order.
- **R18:** only `GameCore` data crosses between the halves.
- **R25:** world state leaves the server through an observer.

This ADR decides:

- where content records live and how they are read;
- what a content identifier is;
- what a *runtime* object identifier is, and why it is a different thing;
- how `Scene.txt` divides between the two halves;
- what a socket's class is.

## Decision

### Record files

- **`Assets/Scene.txt` stays the placement file.** It keeps `model`, `socket`, `instance` and the presentation records (`camera`, `keylight`, `stars`, `engine`, `light`, `build`, `select`).
- **Archetypes go in typed record files under `Assets/Content/`**, one file per family. Each file is added by the first task that needs it:

| File | Family | First needed by |
|---|---|---|
| `Ships.txt` | Ship archetypes: hull, flight characteristics, which model draws them | Slice 1 (task 1.1) |
| `Bindings.txt` | Input bindings | Slice 1 (task 1.5) |
| `System.txt` | Zones and regions | Slice 2 |
| `Weapons.txt`, `Shields.txt`, `Modules.txt`, `Materials.txt`, `Pilots.txt` | Equipment, modules, materials, pilot tiers | Slices 3 and 4 |
| `Encounters.txt`, `Opening.txt` | Raids and the authored opening | Slice 6 |

- **Format.** Every file uses ADR-002's line format through `Neuron::RecordFile`: `keyword key=value …`, with `#` comments. One keyword names one record type. A file may hold several record types of its family, such as a ship and the engines it carries.
- **Typed by its loader.** Each family has one loader in `GameCore`, which turns records into a plain struct. The loader:
  - rejects an unknown keyword or an unknown key;
  - rejects a missing required key;
  - rejects a value outside its stated range.

  Every error names the file and the line, as `Scene.txt` already does.
- **Units.** Physical quantities are in SI units (metres, kilograms, seconds, newtons), and angles are in degrees.
  - A key whose unit is not obvious says it in a comment at the head of its file.
  - The struct field the loader fills carries the unit in its name (R6), such as `massKilograms`.
  - Records do not spell units in their keys, so that the files stay readable to author.

### Content identifiers

- **The identifier is its `name=` value.** It is ASCII, starts with an upper-case letter, and continues with letters and digits: `EscortLancer`, `HangarSection`, `F03`. It is unique within its family. The text a player reads, such as a callsign or a label, is a separate field and is never an identifier.
- **A reference names its family by its key.** For example, `model=` names a model, `archetype=` a ship archetype and `socket=` a socket of a given model. A reference is resolved when content loads, and a dangling name is an error with the file and the line. A content set that loads is therefore one in which every reference resolves.
- **Identifiers are what cross and what persist.** A save names authored content and placed objects by these names (ADR-011). A message that tells the client about an object carries the names it needs to present it, such as its placement name and its model, never an index into a table.

### Runtime object identifiers

- **An `ObjectId` is a 32-bit handle** for one object in one running world. The server assigns it when the object is created and never reuses it while that world lives; 0 means no object.
- **Authored objects take identifiers in placement order** when the world loads. The same content therefore always gives the same identifiers, which tests rely on.
- **An `ObjectId` is not content.** It names an instance of the world, not a definition, so R24's ban on load-order identifiers does not apply to it. It is the handle on the wire: a spawn message pairs an `ObjectId` with the object's content names once, and later messages about that object carry only the `ObjectId`.
- **Nothing persists an `ObjectId`.** A save refers to an authored object by its placement name and to a spawned one by its own saved record (ADR-011). Persisting identifiers across sessions would make every content edit renumber saves, and it would make a save depend on the order in which objects were spawned.
- **Every world object carries its owner tag:** `Neutral`, `Player` or `Hostile`. It is the MVP's stand-in for factions (plan section 11), authored with `owner=` on an `instance` and defaulting to `Neutral`.

### How `Scene.txt` divides

Until the archetype files and later slices take parts of it over, both halves read `Scene.txt`, and each reads its own part:

| Records | Read by | Becoming |
|---|---|---|
| `model`, `socket`, `instance` | Both: the server for placement and poses, the client for models and drawing | Placement stays here. `instance` may later name an archetype instead of a model |
| `ship` (callsign, class, hull, modules) | Server | Archetype in `Ships.txt` (slice 1); hull and module health measured from masks (slice 3) |
| `objective` | Server | The authored opening, `Opening.txt` (slice 6) |
| `resource` | Server | Outpost state (slice 4); `perminute` is removed then |
| `camera`, `keylight`, `stars`, `engine`, `light`, `build`, `select` | Client | Presentation; `engine` and `light` move to the archetype files with their models |

- **The client places nothing itself.** It draws the objects the server announced, at the poses it was sent (plan task F4). It reads placement records only to find, by name, the model and presentation records of an object it has been told about.
- **The server reads each model file for its size.** An attached instance's pose depends on its model's pivot, which is the centre of the model's grid. Slice 3 needs the full voxel grids on the server anyway, for masks.

### Socket classes

- **Every socket has a class:** `dock`, `module`, `equipment` or `clamp`, authored as `class=` on the `socket` record.
- **Each consumer accepts its own class.** A docking point is a `dock` socket (slice 2), a build target a `module` socket (slice 4), a weapon or turret mount an `equipment` socket (slices 3 and 6), and a tug's carrying point a `clamp` socket.
- **Parsing and checking arrive with slice 2.** The key becomes required in the task that first reads it (2.2), and every socket in the shipped scene gains it in that commit.

### The content hash

- **Both halves will compare a hash of the simulation content when a session starts** (GameDesign.md §8, constraint 9).
- **It is computed over the record files the server reads,** after parsing: keyword, keys and values in file order, so that comments and whitespace do not change it.
- It is built with the session handshake in slice 10. Nothing in the MVP compares it.

## Consequences

- **What F4 builds now:**
  - the server loads `Scene.txt` and the model sizes itself;
  - every instance becomes a world object with an `ObjectId`, its placement name, its model, an owner and a pose;
  - `owner=` is parsed on `instance`.
- **What waits for the first task that reads it:** archetype files, socket classes and the content hash.
- **A new family brings a loader, its validation and tests.** The tests cover an unknown key, a missing key, a value out of range and a dangling reference, each refused with the file and line.
- **Renaming content is a breaking change for saves,** because saves name content. A rename after the first release needs a mapping in the save loader. Before the first release, it is free.
- **Load order stays out of everything persistent and every identifier on the wire,** except the runtime `ObjectId`, which is never persisted.
- **Foreclosed:**
  - numeric content identifiers;
  - one file per record;
  - a binary content format in the MVP;
  - the client inventing objects the server did not announce.
