# Outpost:Frontier — game design concept

**Status:** concept, adopted as this repository's design; the decisions in §13 are taken · **Date:** 2026-11-22

This is the design document AGENTS.md refers to, at concept depth. It says what the game is, which systems it is made of, where each comes from, and in what order to build them. It does not fix numbers, write the per-system rules or invent a setting; those follow once this direction is accepted.

**Source.** The concept blueprint in the sibling repository `Outpost.Frontier`: `Blueprint.md` and `blueprint/00` to `12`. It is cited here as "the Frontier blueprint", by chapter and section, and it stays the reference for every rule this document marks as kept. In this document a *build plan* is the cyan wireframe of something not yet built (what `Scene.txt` calls `style=blueprint`), so that "blueprint" always means the source.

**Decisions the owner took on 2026-11-22**, which this document is built on:

| Question | Decision |
|---|---|
| What kind of game | A hybrid: the player flies one ship as in the Frontier blueprint, and also owns and builds an outpost and commands ships through the tactical view |
| Voxel mechanics | Modular station building, destruction as the damage model, asteroid mining. No player ship designer |
| Modes | Single player, designed so that multiplayer can be added without changing rules |
| Depth of this document | Concept and system mapping |

## 1. The game on one page

The player commands a frontier shipyard. They fly one ship themselves, with a cursor, through a universe of a few star systems; they own an outpost bolted to an asteroid, which they extend module by module; and they own a handful of other ships, built in that outpost's yard, which follow orders. Money comes from fighting, trading, jobs and mining. It is spent on the ship, on the outpost and on the fleet, and each of the three makes the other two more useful: the ship protects the tugs, the tugs feed the outpost, the outpost repairs and builds the ships.

Everything solid is voxels, and voxels are the damage model. A hit removes material. A module stops working when enough of it is gone, a ship dies when its core is reached, and a repair puts the same voxels back. Building a station module, repairing a breached hull and mining a rock are the same operation run in different directions.

The Frontier blueprint supplies the rest: drag-dominated flight, the shield collapse window, speed regimes, zones, factions and regard, population generated around the player, fixed prices that differ by place, worth as the measure of progress, missions as triggers over events, and the split between authority and presentation.

## 2. Pillars

The Frontier blueprint has eight pillars (`Blueprint.md` §2). Six are kept as written, one is replaced and one is extended; two are new.

| # | Pillar | Relation to the Frontier blueprint |
|---|---|---|
| 1 | **One ship, one cursor.** The player's own ship goes where its nose points and the guns follow the cursor | Kept (pillar 1). It is the first item on the blueprint's "do not cut" list |
| 2 | **Fly or command.** In space the player flies and leads the wing with a handful of commands; docked, the player commands everything from the tactical view. Time never stops for either, and a ship the player is not flying is flown by its pilot | New. Where the player chooses to be is the resource the hybrid is played with |
| 3 | **Combat in layers, the inner ones made of voxels.** A regenerating shield absorbs everything until it collapses; in the window that follows, structure is carved away and modules fail | Extended (pillar 2). The shield layer is unchanged; hull points, parts and fuses are replaced |
| 4 | **Speed regimes with prices.** Normal flight, thruster, cruise, lanes | Kept (pillar 3) |
| 5 | **Place matters.** Zones say who lives somewhere and how dangerous it is. The outpost's site is a place like any other | Kept (pillar 4) |
| 6 | **Every kill is political.** Standing decides who shoots, who lets the player dock, who sells what, and who raids the outpost | Kept (pillar 5) |
| 7 | **A world that seems to exist without the player.** Traffic is generated around the player and has somewhere to go. The player's own tugs and miners are part of that traffic | Kept (pillar 6) |
| 8 | **A base is its modules.** A station offers a service because it has the module for it, and loses the service when the module is shot off. The player's outpost and every other base are the same kind of thing | Replaces pillar 7, "bases are places" made of staged rooms |
| 9 | **What is built can be broken, and what is broken can be rebuilt.** One voxel state serves construction, damage, repair and mining | New |
| 10 | **Authors build the game, not programmers.** Everything with a name is data; models come from generator recipes | Kept (pillar 8) |

## 3. Five ideas that make it one game

A hybrid of a pilot game and a command game is two games unless most of their machinery is shared. These five identities are the design; if one of them fails in practice, the scope stops being affordable.

### 3.1 One simulation, two views

The Frontier blueprint already says that directives are the one way to tell a ship what to do, and that the player's own manoeuvre buttons, population and missions all issue them to the same manoeuvre layer (ch. 05; `Blueprint.md` §5). The tactical view is nothing more than a second place to issue directives from, to more than one ship. It is available only while docked (D10): in space the player has the flight view and the wing's command keys, and nothing else.

| | Flight view | Tactical view |
| Available | In space | Docked, at any base |
|---|---|---|
| Camera | Chase camera (ch. 09 §3.9) | The orbit camera the sample has |
| The own ship | Flown by cursor: the "free flight" manoeuvre | In its berth. Launching leaves the tactical view |
| Orders | A short command set for the wing: form up, attack my target, defend, dock, hold fire | Select owned ships anywhere in the system, then: go to, attack, defend, dock, join formation, assign an itinerary |
| Other panels | The in-flight HUD (ch. 09 §3.7) | Build, yard, fleet; or a base's services when docked |
| Sees | What the own ship's scanner reports | That, plus what owned ships and the outpost in the same system report |

Three rules follow. Docked is always the tactical view, looking at the station the ship is docked in: this is the base screen of the game, and it is the screen the sample already draws. The tactical view never shows more than owned sensors can see. And neither place may dominate: if commanding from a berth wins a raid as surely as launching does, the player stops flying; if it never does, the tactical view is only a menu. The blueprint's separate accuracy of pilots against players (ch. 05, design notes) and the strength of the outpost's own defences are the dials.

### 3.2 A base is its modules

The Frontier blueprint gives a base rooms, and each room a function (ch. 10 §3.1). It also says what replacing staged rooms with panels costs: "the sense of place; none of the rules". Here the sense of place comes from the station itself, seen from outside, and the rules attach to modules.

| Blueprint room or function | Module | Model in the sample |
|---|---|---|
| Landing pad or deck: launch, repair | Hangar bay, hangar section | `HangarBay`, `HangarSection` |
| Bar: jobs, news, rumours, bribes | Command block | `CommandBlock` |
| Commodity trader | Cargo store, tank cluster | `CargoModule`, `TankCluster` |
| Equipment dealer | Armoury | to be made |
| Ship dealer | Yard | to be made |
| Docking points for large ships | Docking collar | `DockingCollar` |
| Station guns and shield (ch. 01, solar loadouts) | Turret platform, shield generator | to be made |
| — | Refinery, reactor, habitat, sensor mast | to be made |
| — | Truss beam, truss tower: structure that carries sockets | `TrussBeam`, `TrussTower` |

A base definition is a list of modules joined at sockets, which is what the scene file already holds. An authored base and the player's outpost differ only in owner and in who may add modules.

### 3.3 One voxel state

Every solid object is its archetype model plus a mask saying which of the model's voxels are present. Four things change the mask.

| Operation | Direction | Paid with |
|---|---|---|
| Construction | Empty towards the model | Alloy, time |
| Damage | Removes voxels around an impact | The attacker's energy or ammunition |
| Repair | Back towards the model | Alloy at the outpost, money at another base, a repair consumable in the field |
| Mining | Removes rock; ore-bearing voxels become Ore | A mining tool's energy |

What is missing from a mask can be drawn as a build plan, which is how the sample already shows the planned hangar extension. The percentages in the sample's ship panel become measurements: a module's health is the share of its voxels that are present.

### 3.4 Sockets are hardpoints

ADR-002's socket (a named point with a normal and an up vector) and the Frontier blueprint's hardpoint (a named position that accepts equipment of a class, ch. 02 §3.3) are one concept. A socket gets a class. Station modules mount on module sockets, guns and shield generators on equipment sockets, a tug's cargo on its clamp, and a docking point is a socket with a docking class (ch. 04 §3.2). "A hull is valuable for the grades it accepts" then holds for a station as well as for a ship.

### 3.5 Outpost resources are goods

The sample's HUD shows Alloy, Ore, Water, Power and Crew. They are not a second economy.

| Resource | What it is | Blueprint concept it reuses |
|---|---|---|
| Ore | A commodity. Mined, bought or sold | Commodity; mined loot (ch. 01 §3.6, ch. 07 §3.8) |
| Alloy | A commodity. Refined from Ore at the outpost, or bought. Construction and repair consume it | Commodity |
| Water | A commodity. Crew consume it | Commodity |
| Power | The outpost's power pool: reactors fill it, modules draw on it | Power pool of an equipped object (ch. 02 §3.5) |
| Crew | A capacity. Habitats provide it; every working module and every owned ship needs some | New |

Ore, Alloy and Water sit in the same price map as every other commodity, travel in the same holds and are sold at the same bases. Money stays the one currency. Worth, the blueprint's single measure of progress (ch. 07 §3.14), is extended to include the outpost's modules, the owned ships and the stockpile.

## 4. Core loops

**Seconds: the fight.** As in the Frontier blueprint: steer with the cursor, lead the target, fire while the power pool lasts, break the shield. Then choose where to cut: engines to stop it, weapons to silence it, or straight for the core. One key sends the wing at the target.

**Minutes: the sortie.** Launch from a base. Ride lanes and cruise. Meet traffic whose regard decides whether it talks, scans or attacks. Fight or flee; tractor in the loot and the wreckage. Dock, and the view turns to the station: sell, repair, refit, read the job board. Launch again.

**Minutes: the shift at the outpost.** Dock at home. Place a build plan on a free socket. See what the miners brought in and what the refinery made of it. Queue a ship in the yard, change a tug's run, patch what the last raid broke. If a raid arrives, either direct the defence from the tactical view or launch and fight it.

**Hours: the climb.** Worth grows from all sides. A better ship reaches more dangerous places with better profit and loot. A bigger outpost repairs faster, builds stronger escorts and turns Ore into something worth hauling. A bigger wing makes a harder job possible.

**The whole game: the campaign.** Scripted missions lead the player across the map, open locked jump points and are paced by worth gates between missions (ch. 08 §3.10). Outpost events are mission events like any other: the three objectives on the sample's HUD, "defend the shipyard", "finish hangar extension F-03" and "escort tug T-041 to dock", are the pattern.

## 5. The player's journey

The state diagram of `Blueprint.md` §3 holds, with these changes.

| State | Change |
|---|---|
| Base | No rooms. The tactical view of the station, with one panel per service the station's modules provide |
| Base, at the player's outpost | The same, plus build, yard and fleet |
| Space: free flight | Unchanged |
| Space: manoeuvre | Unchanged |
| Space: lane ride, docking sequence | Unchanged. Followers in formation receive the same order (ch. 05 §3.10) |
| Cutscene | Scripted camera in space only; no acted scenes in rooms |
| Death | Back to the last base with the ship as fitted there, cargo lost (D1, decided) |

## 6. System mapping

For each chapter of the Frontier blueprint: what is kept as written, what is adapted, what is replaced and what is cut. "Kept" means the chapter is the specification until a Voxel chapter supersedes it.

| Ch. | System | Verdict | What changes here |
|---|---|---|---|
| 01 | World and universe | Kept, smaller | Zones, immersion, hazards, connections, system paths, knowledge and discovery as written. Fields: the generated lattice is kept for ambient rock, which is not saved; a *claim* asteroid is an authored solar whose mask is saved. Fewer systems (§10). Zone roles become explicit fields, as the chapter's design notes advise |
| 02 | Ships, equipment and flight | Flight kept; object model adapted | The flight model, the two pools, cruise and engine kill are untouched: chapters 05 and 06 depend on them. Parts become modules (voxel regions); hardpoints become classed sockets (§3.4). A fixed tick replaces the variable step. No physics library: the two formulas plus swept simple shapes, as the chapter allows |
| 03 | Combat and damage | Outer layer kept; inner layer replaced | Firing cycle, energy budget, bolts, lead, shields, collapse window and cruise disruption as written. Hull hit points, parts, armour divisor and fuses are replaced by §7.1. Guided munitions, mines and countermeasures are kept but come late |
| 04 | Travel, docking and navigation | Kept | The dock request with its refusals, the approach, launch, lanes that can be broken, jumps, routes and locks as written. Docking points are sockets. The player's outpost answers dock requests like any base |
| 05 | AI and pilots | Kept, reduced, extended | The manoeuvre layer, behaviours that veto themselves, target selection, fire control and formations as written, with the reduced set the chapter recommends: attack pass, evade, trail, formation, flee, travel, and four tiers of one pilot per role. Added: directives from the tactical view; utility itineraries (mine, haul); aiming at a module instead of a part |
| 06 | Population and encounters | Kept, extended | Population follows players; class and level indirection; itineraries; battle cap and relief time as written. Added: an "assault a solar" itinerary for raids; the player's utility ships as owned traffic. Spliced speech is cut; comms are text |
| 07 | Factions, economy and progression | Kept, extended | Regard, thresholds, empathy, personal adjustments, fixed prices by place, markets, worth and level as written. Added: Ore, Alloy and Water in the price map; module plans as goods sold under the same level and standing conditions as equipment; worth includes outpost and fleet; one level number instead of two |
| 08 | Missions and base services | Kept, extended | Missions as content controllers, triggers, labels, directive lists, objectives, checkpoints, generated jobs and the job board as written. Added events: module completed, module destroyed, stockpile reached, ship built. Added actions: raid a solar, grant a plan, open a claim. The offer tree becomes a flat list of templates. Inhabitants become entries in a text panel; rumours and bribes keep their rules |
| 09 | In-flight interface and presentation | Kept, restyled | The command layer, one cursor for pointing, steering and aiming, rate control, the scanner as the single source of contacts, the manoeuvre bar. The look is the sample's command-and-control HUD. Added: wing commands and the view toggle. The cockpit view is cut. Sub-targeting is kept, because choosing which module to cut is the point of §7.1 |
| 10 | Bases, scenes, menus and text | Mostly replaced | Rooms, sets, placements, costumes, acted conversations and room scene scripts are cut. Kept: the base visit as a state machine, the service and inventory panels, the map, text by identifier, knowledge granted by reading |
| 11 | Architecture, networking and persistence | Kept, tightened | Authority and presentation, one message catalogue, the event bus, systems that run only where a player is, the character as one record. Every "do not copy" of the chapter's §7 is followed from the start (§8). The network transport is MsQuic, not a reliable ordered channel for everything (§8.1). The repository already has the seam: the client draws a snapshot and never reads `World` |
| 12 | Data model and content census | The proposal is adopted | The chapter's eleven-point modern form, not the original's format: typed records, checked references, no load order, templates and inheritance. `Scene.txt` and `RecordFile` are the starting point. The census is the basis of §10 |

Cut outright, each named as affordable in `Blueprint.md` §9 or in its chapter's design notes: staged rooms, acted conversations, fragment-spliced speech, the offer tree, the cockpit view, a physics library. Also cut: the cloak device, which only NPCs carry. Cut by the owner's decision: a player ship designer. Not cut, although the blueprint lists them as cuttable: lanes as real travel, damage to parts of a ship, itineraries, empathy, approach flight when docking, discovery. Each of those is where this game's own systems plug in.

## 7. Systems the Frontier blueprint does not have

### 7.1 Voxel structure and damage

- **Materials.** Each palette family (ADR-002) is a material with a strength, a mass and a yield. Armour is a stronger outer material, not a divisor. This sits beside the emissive table in `GameCore/Palette`.
- **Modules.** A module is a named region of a model's voxels with a function: reactor, engines, weapons mount, sensors, hangar, refinery. Its health is the share of its voxels present. Below one threshold it works at a reduced rate, below a second it stops. The engine rule of the blueprint (ch. 02 §3.4: no nozzle left, no thrust) becomes the general rule.
- **The core.** Every ship and station has one core module. A ship whose core stops is destroyed and breaks up; this replaces "hull at zero". "Hull" on the HUD is the share of structural voxels present.
- **Separation.** Voxels that lose their connection to the core become a wreck: a drifting object that can be salvaged. This replaces shed parts and dropped equipment. At scale, connectivity is tracked incrementally, not refilled from the core on every hit (§8.2).
- **Damage events.** The blueprint's damage list (ch. 03 §2) becomes a damage event: object, impact position, shape, energy, cause, inflictor. Weapons, explosions, collisions and hazard zones all produce them. The authority decides which voxels go and sends the event; the presentation replays it.
- **Shields** are untouched: no bleed-through, collapse at a threshold, a rebuild time, energy damage drains the pool that rebuilds them.
- **Repair.** A hangar restores a docked ship's mask and a station's own modules, consuming Alloy at the outpost or money elsewhere (ch. 07 §3.12 gives the price rule). A repair consumable patches structure in the field and leaves modules for a hangar, as the blueprint's hull item leaves parts (ch. 03 §3.10).
- **Calibration target.** A duel must keep the blueprint's rhythm: seconds to break a shield, a few more seconds of exposure, a decision inside the window. Time to core replaces time to zero hull, and the blueprint's protection of the player's own parts and equipment (three and five times tougher, ch. 03 §3.4) is kept as a material factor on what a player owns.

### 7.2 The outpost

- **Claim.** An authored site: an anchor asteroid, an exclusion zone, a starting command block with free sockets. The first release has one, in the starting region.
- **Building.** The player picks a module they hold the plan for and a free socket that accepts it. The build plan appears; Alloy is consumed as the mask fills; the module starts working when complete and brings sockets of its own. A module can instead be bought prefabricated at another base and hauled in by a tug, which is faster, costs money and is exposed on the way.
- **Running.** Reactors fill the power pool and modules draw on it. Habitats set crew capacity; modules and owned ships need crew; crew drink Water. A shortage switches modules off in a stated priority order; nothing is destroyed by a shortage.
- **Services.** Whatever §3.2 lists for the modules present. An outpost without a hangar cannot repair; one without a yard cannot build ships.
- **Defence.** Turret platforms and a shield generator are equipment on sockets, flown by a pilot record like any armed solar (ch. 01 §3.3). Escorts can be left on a defend directive.
- **Raids.** A raid is an encounter with the itinerary "assault this solar". Like all population it is generated around a player who is present, from the zone's encounter lines, and only by factions hostile to the owner. Friendly factions nearby take the owner's side by the blueprint's side-taking rule (ch. 07 §3.5).
- **Away.** A system without a player does not run (ch. 11). While the owner is elsewhere the outpost advances by a ledger: the same production, consumption and haulage rates, computed from the same modules and assignments, with no combat. The sample's `World`, which today adds income per minute in whole ticks, is that ledger.

### 7.3 Mining

A mining tool is a launcher whose damage is high against rock and low against everything else. Carving an ore-bearing voxel yields Ore, as loot to tractor in or directly into a miner's hold. Ambient field rock is generated from position, as in the blueprint, and forgets what was done to it when its cube unloads; the claim asteroid remembers. An owned miner assigned to the claim does the same work on an itinerary.

### 7.4 The fleet

| Kind | What the player does with it | How it behaves |
|---|---|---|
| Own ship | Flies it. Changes it only while docked at the outpost | Free flight, or the manoeuvre layer under a directive |
| Wing | Orders it with command keys in flight, or from the tactical view while docked | Escorts in formation on the own ship. They dock, ride lanes and jump with it |
| Utility ships | Assigns an itinerary once | Tugs and miners run it unattended: claim to outpost, outpost to a base and back |
| Garrison | Sets a defend directive | Stays at the outpost |

Every owned ship is a blueprint NPC ship definition (ch. 06 §3.6): an archetype, a loadout and a pilot, with the player as owner. It is built in the yard from a hull the player has bought the plan for, and refitted there under the ordinary hardpoint rules. Crew capacity caps the fleet. The numbers stay small on purpose: a wing of about three, a few utility ships.

## 8. Rules that keep multiplayer possible

Single player ships first. These constraints apply from the first slice, because the Frontier blueprint is explicit that the split costs little early and a great deal late. Constraints 1 to 7 and 9 shape code and are written in AGENTS.md as R18 to R24, and §8.2 adds R25; 8 and 10 are game behaviour and stay here.

1. **One message catalogue.** Presentation and authority exchange messages and never call each other. The in-process transport and the network transport, MsQuic (§8.1), carry the same messages. `GameApp` does not reference `GameLogic`; that edge is already forbidden.
2. **The authority decides.** Damage, purchases, builds, mining yields, docking, standing and discovery are computed by the authority from commands. The original's trust in the player's machine (ch. 11 §3.8) is not copied.
3. **The own ship is predicted, and bounded.** The presentation integrates the player's ship from input and is not corrected in normal play; the authority checks speed, fire rate and energy against the shared model.
4. **A fixed tick is the clock.** `TickClock` already provides it. No rule reads wall time.
5. **Masks change only by data from the authority:** damage events applied with integer arithmetic so that both sides arrive at the same voxels, or exact snapshots and brick diffs that name the last event they include (§8.2). This is the one part of the game that must be deterministic (R16), and it needs an ADR before slice 3.
6. **No rule depends on pausing, on reloading or on seeing the whole system.** The view switch does not stop time; death has one rule for both modes; the tactical view shows sensor coverage only.
7. **State belongs to a character or to the world,** never to the process: locks, ambient switches, knowledge, the outpost and the fleet.
8. **What an absent player owns advances by ledger and cannot be attacked.**
9. **Content has stable text identifiers,** and both sides compare a hash of the simulation data when a session starts.
10. **A character is one self-contained record,** including the outpost's modules and masks and the fleet; content saves its own part.

### 8.1 The network transport: MsQuic

When the game goes over a network, it goes over QUIC, using Microsoft's MsQuic library. Single player never uses it: the authority and the presentation in one process keep the in-process transport, which is also what every test runs on. The owner decided this on 2026-11-22, and with it the three choices below; it is the first library approved under AGENTS.md R14.

QUIC answers the Frontier blueprint's list of what not to copy in one piece (ch. 11 §7). The original sent everything, positions included, reliable and in order, and had to throttle itself with a saturation gate because a stale update could not be dropped; its wire had no length, version, integrity check or encryption; and its account identifier went in clear as a bearer token. QUIC gives encrypted, authenticated connections over TLS 1.3, congestion control, reliable streams and unreliable datagrams on one connection, and survives a change of client address.

| Concern | How the game uses MsQuic |
|---|---|
| Sessions | One QUIC connection per player session |
| Commands, discrete events, inventory, chat | Reliable streams. Order matters for these, and the blueprint already treats them as facts sent once (ch. 11 §3.7). Unrelated traffic goes on separate streams, so that a lost packet stalls only its own stream |
| Flight input | Datagrams, stamped with the tick they are for, each repeating the last few inputs so that one loss costs nothing; the server buffers a few ticks against jitter (§8.2) |
| Object state | Unreliable datagrams (RFC 9221), each carrying a sequence number; a receiver keeps the newest and discards older ones. A lost state update is replaced by the next, never retransmitted. What goes into each datagram is chosen per observer by interest, priority and a byte budget, delta-compressed against the last state the client acknowledged (§8.2). QUIC's congestion control replaces the blueprint's saturation gate: datagrams are built when the connection can send, and the send state MsQuic reports for each one tells the server what was lost |
| Voxel damage events | Reliable streams sharded by target object: R22 needs every event in order per object, not one global order, so a lost packet delays only the objects on its stream. Observers far from an object receive coalesced brick diffs at a low rate instead; a mask snapshot brings a newcomer up to date (§8.2) |
| Message size | A state message must fit one datagram. Until slice 10 the budget is 1,100 bytes: QUIC guarantees a 1,200-byte UDP payload, less its own packet and frame overhead (ADR-008). From slice 10 it is the maximum MsQuic reports for the connection. State for many objects is split across datagrams and never assumes they arrive together |
| Encoding | Every message carries its kind, its length and the catalogue version, as constraint 9's data hash and the blueprint's design notes require |
| Identity and trust | The host proves itself with a self-signed certificate, and the client pins it on first connection and warns if it ever changes (decided). There is no certificate authority to configure, and every connection after the first is protected. Accounts authenticate over the encrypted stream, never by a bare identifier |
| Threads | MsQuic calls back on its own worker threads. Callbacks only enqueue; the tick drains the queue at its first step (ch. 11 §3.2), so content and rules stay on one thread |
| Platform | The Schannel build (decided). It uses Windows' own TLS 1.3, which exists from Windows 11 and Windows Server 2022, so that is the floor for networked play; single player keeps today's. Schannel does not offer QUIC's 0-RTT reconnect, which the game does not need |
| Acquisition | The NuGet package `Microsoft.Native.Quic.MsQuic.Schannel`, at an exact pinned version (decided), restored by NuGet, the repository's package manager |

[ADR-017](ADR/ADR-017-nuget-and-the-msquic-transport.md) decided the rest:

- A project pins the package in `packages.config` and imports only the package's own build file, and CI restores before it builds.
- `msquic.dll` is loaded at run time from the executable's folder and never linked, so single player and the test suites never load it (AGENTS.md R14).
- State datagrams are built only when the connection can send.

The MVP's spike (ImplementationPlan.md, task F7) proved this shape against the real catalogue.

### 8.2 Scale target: 100 players in one scene

The design must still work when a host holds 100 players and all of them meet in one place. This section was written as a review of the rest of the design against that case, on 2026-11-22. Where the design as first written would not have held, the fix is stated here, and the sections it touches point back to this one. Every figure below is a target or arithmetic, not a measurement; the plan's load tests have to confirm or overturn them.

**The load.** 100 players in one system, all within a few kilometres of each other: one battle, or one crowded hub. Each flies a ship and leads a wing of up to three (D12), so 400 player-owned ships; raiders and other traffic add about 200 more. Up to 20 outposts are in view. Wrecks and debris come and go.

**What that costs to draw.** Measured from the committed models: an escort has about 2,000 surface voxels, the frigate 17,000, the outpost as the sample shows it about 48,000 plus 52,000 for its claim asteroid. The sample draws about 128,000 surface voxels a frame. The load above is roughly 1.2 million for the ships and 2 to 6 million for 20 outposts as they grow, so 25 to 55 times today's figure, before damage: carving exposes interior voxels, so a shredded hull has several times its intact surface. The renderer of ADR-003 costs a vertex-stage quad per drawn surface voxel per pass, at every distance, and draws the scene twice (main pass and shadow map).

**What that costs to send.** A ship's state, quantized, is about 20 bytes. Sending all 600 to every client at 20 Hz is about 1.9 Mbit/s per client and 190 Mbit/s from the host. A battle produces some 300 to 1,600 voxel damage events a second at about 24 bytes each, which every client that sees the target needs: up to 0.3 Mbit/s per client, 30 Mbit/s from the host. Input from 100 clients is negligible.

**Verdict.** The design's seams are right: the split into server and client, one message catalogue, damage as integer events (R22), QUIC with streams and datagrams, and the blueprint's observers and dead reckoning. As first written, six things would not have held, and they are now part of the design:

| # | What would break | What the design now says |
|---|---|---|
| 1 | **Drawing cost grows with everything in range.** No level of detail, no culling beyond per-voxel tests, one shadow map fitted to the whole scene and redrawn whole, a fixed list of 16 point lights | Voxel level of detail: each brick keeps coarser versions (2×, 4×, 8×), chosen by projected voxel size, so distant objects cost a fraction of their full surface. Bricks are culled against the view and, later, against occlusion, on the GPU, with indirect draws. Shadows are cascades around the camera, with static structures cached and casters drawn at reduced detail. Lights are gathered per screen cluster, so engines, muzzle flashes, breaches and explosions are not capped at 16. Effects come from GPU particle pools with a global budget |
| 2 | **Re-meshing per damage event.** Re-extracting a whole object for every hit does not survive hundreds of events a second | Damageable models are always stored as 16³ bricks. Events mark bricks dirty; dirty bricks are re-extracted at most once a frame, closest and largest on screen first, within a time budget. The mask itself is always current; only its drawing may lag a frame or two at a distance |
| 3 | **Connectivity by flood fill from the core after every event.** A full-grid fill per hit multiplies into hundreds of millions of cell visits a second | Connectivity is incremental: a graph of bricks and their contacts, a local fill only where a removal could cut a bridge, and separation decided a tick later. A wreck appearing a tick late is invisible. The MVP starts with the simple fill behind the same function, since a duel needs no more |
| 4 | **Everything to everyone, and one stream.** A whole-world snapshot does not fit 100 clients' bandwidth, and one reliable stream lets a single lost packet stall every command and damage event behind it | Each client is an **observer** (blueprint ch. 11 §3.7), also in single player: the server picks per observer which objects it knows (interest), sends the most important first (priority by distance, relevance and staleness), within a byte budget per client, delta-compressed against the last state the client acknowledged, and only when dead reckoning would be wrong. The simulation runs at its tick rate; state is sent at 20 to 30 Hz. Order is kept per target object, not globally, so damage events travel on several streams sharded by object, and per-tick flight input travels in datagrams that each repeat the last few inputs. (AGENTS.md R25) |
| 5 | **Masks only by event.** A player who comes into range of an outpost carved for an hour, or joins late, cannot replay an hour of events | A mask is brought up to date by an exact snapshot, or by a diff of dirty bricks, that names the last event it includes; events then continue from there. Observers far from an object get coalesced brick diffs at a low rate instead of every event. Joining streams outposts and their masks progressively, nearest first (AGENTS.md R22) |
| 6 | **Hits against the server's present.** With slow bolts and 100 to 200 ms of round trip, a shot that hits on the shooter's screen misses on the server unless the server allows for what the shooter saw | In slice 10 the server keeps a short pose history for every object and adds lag compensation: the first part of a bolt's flight is traced against targets rewound to the shooter's view, with a bounded rewind so that nobody is hit behind cover long after the fact |

Four further points complete the picture:

- **Positions.** A developed system is 80 to 100 km across, where a 32-bit float resolves only to about 8 mm. Positions are held in double precision (or an equivalent fixed-point form) on both sides; the renderer works relative to the camera; the wire quantizes positions relative to a coarse cell.
- **The server's tick.** The rules stay single-threaded and in a fixed order, as the blueprint has them. Replication then reads the world without changing it, so the 100 observers are encoded in parallel. Bolts are traced through a spatial index, and voxels are walked only inside candidate bricks.
- **Population and raids per place, not per player.** The blueprint generates traffic and caps battle size around each player (ch. 06). Around 100 players in one place that would spawn a hundred times the traffic. In multiplayer the budget belongs to the zone: one population and one battle cap per area, shared by every player in it.
- **The host.** 100 players need a dedicated, headless server with roughly 50 to 100 Mbit/s of upstream after the measures above. A home connection is realistic for about a dozen players. The server already builds without Direct3D; a headless server executable joins the solution in slice 10.

**Budgets to validate.** Client: 60 frames a second at 1920×1080 on the reference GPU in the load above. Server: the simulation tick within half its period on one core, replication alongside. Network: at most about 0.5 Mbit/s per client in the worst case and about 0.15 Mbit/s typical. A crowd benchmark in the MVP's foundations decides early whether the per-voxel rendering technique of ADR-003 can reach the client budget with levels of detail, or must give way to triangle meshes built per brick (plan task F6). A 100-bot load test over MsQuic gates slice 10.

**When each part is built.** The target is stated now, because stating it costs nothing and prevents choices that would block it. What is built in the MVP is only what would mean rewriting core structures later: double-precision positions, bricks, the observer path, delivery classes and mask snapshots, and the renderer go/no-go. Everything else in this section is added behind those interfaces in slices 7 and 10 (plan section 14).

**Outposts per system** are bounded by D13: about twenty authored claim sites per system, the figure the drawing budget above assumes.

## 9. How it lands in the repository

| Project | Takes on |
|---|---|
| `NeuronCore` | Voxel masks, the carving and connectivity arithmetic, typed records and reference checking; the transport interface and the in-process transport, which client and server both need and which neither may take from the other's library. The MsQuic transport (§8.1) sits here too, or in a new engine library that both halves reference. ADR-017 sets the bounds, and slice 10 makes the choice |
| `NeuronServer` | The tick, listening for and admitting sessions, observers with interest, priority and per-client budgets (§8.2), save infrastructure |
| `NeuronClient` | Rendering of voxels that change, in bricks with levels of detail, culled on the GPU, with cascaded shadows and clustered lights (§8.2); the chase camera beside the orbit camera, devices to commands, audio, connecting to a host |
| `GameCore` | What both sides share: archetypes, materials, the flight and equipment model, zones, the message catalogue |
| `GameLogic` | The authority: systems, world objects, regard, markets, the outpost and its ledger, and the content controllers for population, missions and the campaign |
| `GameApp` | The presentation: flight view, tactical view, HUD, base panels, the map |
| `AssetGenerator` | Recipes for families of hulls and modules, so that content volume is generated, not drawn |

Decisions this design forces, each an ADR before the slice that needs it: the deterministic mask and the damage event; the shared flight model on a fixed tick; the record schema that grows `Scene.txt` into system, base and archetype definitions; the message catalogue; the network transport over MsQuic; replication through observers; levels of detail and culling; lag compensation; rendering of changing voxels (today the surface is extracted once and the shadow map is drawn once); world scale. On scale: the sample's ship models, read as one metre per voxel, give escorts of 42 to 52 m and a frigate of about 190 m, which fits the blueprint's metric constants, so those can be carried over unchanged.

## 10. Content budget

A proposal, to be confirmed. It keeps the blueprint's ratios (`Blueprint.md` §8) at between a seventh and a tenth of its size, which is a guess at what one developer with generators can author.

| Kind | Frontier blueprint | Proposed |
|---|---|---|
| Systems | 53 | 6 to 8 |
| Bases | 197, median 3 per system | About 18, each a module list |
| Station modules | — | About 15 models |
| Factions | 55 | 8: a lawful and an unlawful bloc per region |
| Ship archetypes | 69, of which 31 purchasable | About 12; 5 the player can fly |
| Guns | 513 | 4 families by 5 grades, from templates |
| Shields | 123 | 3 families by 5 grades |
| Commodities | 40 | 12, including Ore, Alloy and Water |
| Encounter definitions | 42 | About 10 |
| Pilots | 64 | 4 tiers by 4 roles |
| Campaign | 13 missions | 5 or 6 |
| Generated missions | 1 type | 2: destroy at a site; defend a solar |

## 11. Build order

Vertical slices, each playable, after the Frontier blueprint's own order (`Blueprint.md` §7). The first six make the sample scene playable: its three HUD objectives become things the player can actually complete. The universe comes after that, because the outpost half is where this game differs and where the existing assets are.

| Slice | Result | Draws on | It works when |
|---|---|---|---|
| 0. A scene | The tactical sample | Done | — |
| 1. A ship that flies | An escort flown by cursor around the shipyard: throttle, strafe, thruster, engine kill, cruise, chase camera | 02 §3.7 to 3.10; 09 §3.1 to 3.5, 3.9; 11 | Top speed and turn rate come out of thrust and drag as ch. 02 predicts. Another ship obeys a go-to directive through the same manoeuvre layer |
| 2. A place to be | Dock at the shipyard and launch again; docked is the tactical view; one system with zones and a hazard; save on launch | 01 §3.1 to 3.5; 04 §3.1 to 3.5; 11 §3.10 | A full loop of launch, fly, dock and resume from a save |
| 3. A fight in voxels | Guns, shield, damage events, modules that fail, core death, wrecks; one opponent that attacks, evades and flees; the combat HUD | 03 §3.1 to 3.5; 05 §3.1 to 3.8; 09 §3.6 to 3.8; §7.1 | A duel has the rhythm of shield break and window. The ship panel's percentages are measured from masks. Carving holds the frame rate |
| 4. An outpost that grows | Sockets, build plans, Alloy, modules with services, power, crew and Water; repair | §3.2 to 3.5; §7.2 | "Finish hangar extension F-03" is completed by building it. A module shot off takes its service with it and can be rebuilt |
| 5. Mining and haulage | Carving rock into Ore; tractor; miner and tug itineraries; refinery; the ledger | 01 §3.6; 03 §3.9; 06 §3.8; §7.3 | The outpost sustains itself from the claim, present or away, at the same rate |
| 6. A wing | The yard builds escorts; formation; wing commands; tactical orders; raids | 05 §3.9 to 3.11; 06; §7.4 | "Defend the shipyard" can be won from the cockpit or from the tactical view while docked at the outpost, and "escort tug T-041 to dock" can be lost |
| 7. A living system | Factions and regard; population with patrols and traders that dock; cargo scans; text comms | 06 §3.2 to 3.8; 07 §3.1 to 3.6 | Shooting one faction's ship changes how two others treat the player and who raids the outpost |
| 8. An economy and a universe | Goods, markets, dealers, worth and level; several systems, jumps, lanes, routes, locks; the map and discovery | 07 §3.8 to 3.15; 01 §3.6 to 3.10; 04 §3.6 to 3.10 | A trade run, a fight and a shift at the outpost all raise worth. A route across three systems can be plotted and flown |
| 9. Jobs and a campaign | The trigger language, the job board, generated and scripted missions, checkpoints | 08 | An author builds an escort-and-ambush mission that ends at the outpost, from data alone |
| 10. Multiplayer | The MsQuic transport, observers with interest, priority and budgets, lag compensation, accounts, a headless server | 11 §3.6 to 3.13; §8.1, §8.2 | Two players meet, fight the same ships and see the same holes in them; a 100-bot load test holds §8.2's budgets |

The first six slices are the MVP; [ImplementationPlan.md](ImplementationPlan.md) breaks them into work. Slices 1 to 3 prove the feel and carry the largest technical risk. Nothing after them should start until a duel is good.

## 12. Where this design is weakest

1. **Scope.** The hybrid was the largest of the three options on offer. The design answers with the five identities of §3, the cut list of §6 and the budget of §10, but two views still mean two cameras, two interfaces and a pilot AI that has to serve as opponent, wingman and stand-in for the player. Slice 6 is the honest checkpoint: if the first six slices have not produced a game worth playing in one scene, the universe will not rescue it.
2. **Home against frontier.** The blueprint's difficulty curve is a journey outward: danger and profit grow with distance and the player rarely goes back. An outpost is an anchor. The further the player ranges, the longer the trip home; the more they stay home, the less the map matters. A small map, a central claim and lanes soften this and do not remove it. D3 answers it with one claim now and forward claims further out as campaign rewards, so that the anchor moves outward with the player. Whether that is enough is untested.
3. **Destruction as the damage model.** It has to give a readable fight with the blueprint's rhythm, hold the frame rate while surfaces change, and replay identically on two machines. The sample's renderer assumes a static scene. This is why slice 3 comes before any outpost work.
4. **Fly or command.** With the tactical view only in a berth (D10), the choice is made by docking or launching. If staying docked and commanding wins a raid as reliably as flying, the flight half is optional; if commanding never pays, the command half is. And in space the player cannot reassign a tug or a miner at all, which the wing's command keys do not cover. The balance will take more than one try.
5. **An unbounded faucet.** The blueprint's economy has fixed prices and unlimited stock, and stays bounded because a hold is small and a trip takes time. Mining and refining add a source that runs while the player does something else. Haulage throughput has to be the limit, and the ledger must never pay better than being present.
6. **Leaving as a tactic.** If raids happen only around a present player, a player in trouble can jump out to stop one. Constraint 8 needs a companion rule for a raid already in progress.
7. **What the blueprint does not know.** Rendering, effects and audio are requirements only (ch. 09 §3.10 to 3.14), and campaign failure was never recovered (ch. 08). Those gaps are inherited.
8. **A hundred players is a target, not a measurement.** §8.2 changes the design so that nothing in it prevents the scale, and states budgets. Whether the voxel renderer reaches them with levels of detail, and whether a hobby project can run a dedicated host, are open until the crowd benchmark and the load test say so.

## 13. Open decisions

*D1 to D12 were decided by the owner on 2026-11-22: D1, D2, D5, D6 and D12 for the MVP plan ([ImplementationPlan.md](ImplementationPlan.md)), the rest the same day. Eleven follow the recommendation; D10 does not, and the rest of this document follows the decision. D13 came out of the scale review (§8.2) and was decided the same day, as recommended.*

| # | Question | Decision |
|---|---|---|
| D1 | What death costs — *decided* | One rule for both modes, the blueprint's multiplayer rule (ch. 11 §3.12): back to the last base with the ship as it was fitted there, cargo lost. Reloading a save then stays a convenience and is not a rule |
| D2 | Can the outpost be lost — *decided* | No. The command block is the blueprint's indestructible solar: everything attached to it can be shot off, the claim itself survives |
| D3 | One claim or several — *decided* | One in the first release. Forward claims further out as campaign rewards, to answer weakness 2 |
| D4 | What sizes a raid — *decided* | The place and the owner's standings, as for all population. Not the outpost's worth: the blueprint keeps danger a property of the map |
| D5 | The HUD's DESIGN tab — *decided* | Rename it YARD: build and refit ships from bought hull plans. With no ship designer it has nothing to design |
| D6 | Which ships the player may fly — *decided* | Fighters and freighters. Frigates are owned and commanded, not piloted: cursor flight is tuned for hulls that turn at 23 to 96 degrees a second |
| D7 | Can the outpost sell to visiting traffic — *decided* | No. Surplus is hauled to a base, by the player or a tug. Profit stays a matter of knowing the map |
| D8 | The setting — *decided* | Not part of this document. The sample's names (Kessler Reach, Shipyard Command, F-317) are working titles; the MVP needs no setting. The setting is its own document, written before slice 7 brings factions |
| D9 | Campaign in the first release — *decided* | A short one (§10), since worth gates and locked jump points are what pace the open game |
| D10 | Where the tactical view is available — *decided* | Only while docked, at any base, limited to owned sensors. In space the player flies and has the wing's command keys. (The recommendation was anywhere; the owner chose otherwise) |
| D11 | Voice — *decided* | None at first. Comms are text; the rules of chatter (ch. 06 §3.12) are kept |
| D12 | Wing size — *decided* | About three, capped by crew |
| D13 | How many outposts one system can hold — *decided* | A fixed set of authored claim sites per system, about twenty, so that a crowded system has a bounded amount of player-built structure to draw and send (§8.2). In multiplayer, claims are first come, first served |

## 14. Basis

Read in full for this document: `Blueprint.md` and blueprint chapters 00, 01, 02, 03, 07 and 11. Read for purpose, structure and design notes, with selected sections: chapters 04, 05, 06, 08, 09, 10 and 12. From this repository: `README.md`, `AGENTS.md`, ADR-001 to ADR-003, `Assets/Scene.txt`, `GameLogic/World.h`, `GameCore/WorldSnapshot.h`, `GameCore/Palette.h` and the reference screenshot. Model sizes in §9 were read from the `SIZE` chunks of the committed `.vox` files. For §8.1: MsQuic's own documentation (`README.md`, `docs/Platforms.md`, `docs/API.md`, `docs/Release.md` in `github.com/microsoft/msquic`) and Microsoft Learn's MsQuic and Schannel pages, read on 2026-11-22.

Nothing here was built or run. Every number in §10 is a proposal, and every blueprint number quoted is the blueprint's own.
