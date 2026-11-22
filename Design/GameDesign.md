# Outpost:Frontier — game design concept

**Status:** concept, adopted as this repository's design; the decisions in §13 are open · **Date:** 2026-11-22

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
| 2 | **Fly or command.** The same world has a flight view and a tactical view. The player is in one at a time, time never stops for the switch, and a ship the player is not flying is flown by its pilot | New. Attention is the resource the hybrid is played with |
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

The Frontier blueprint already says that directives are the one way to tell a ship what to do, and that the player's own manoeuvre buttons, population and missions all issue them to the same manoeuvre layer (ch. 05; `Blueprint.md` §5). The tactical view is nothing more than a second place to issue directives from, to more than one ship.

| | Flight view | Tactical view |
|---|---|---|
| Camera | Chase camera (ch. 09 §3.9) | The orbit camera the sample has |
| The own ship | Flown by cursor: the "free flight" manoeuvre | Flown by its manoeuvre layer under the last directive; with none, it holds position |
| Orders | A short command set for the wing: form up, attack my target, defend, dock, hold fire | Select ships, then: go to, attack, defend, dock, join formation, assign an itinerary |
| Other panels | The in-flight HUD (ch. 09 §3.7) | Build, yard, fleet; or a base's services when docked |
| Sees | What the own ship's scanner reports | That, plus what owned ships and the outpost in the same system report |

Three rules follow. Docked is always the tactical view, looking at the station the ship is docked in: this is the base screen of the game, and it is the screen the sample already draws. The tactical view never shows more than owned sensors can see. And the own ship must fight better in the player's hands than under a directive, or nobody will fly; the blueprint's separate accuracy of pilots against players (ch. 05, design notes) is the dial.

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
| Space: manoeuvre | Unchanged, and it is also the state the own ship is in whenever the player is in the tactical view |
| Space: tactical view | New. Entered and left with one key at any time |
| Space: lane ride, docking sequence | Unchanged. Followers in formation receive the same order (ch. 05 §3.10) |
| Cutscene | Scripted camera in space only; no acted scenes in rooms |
| Death | See open decision D1 |

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
| 11 | Architecture, networking and persistence | Kept, tightened | Authority and presentation, one message catalogue, the event bus, systems that run only where a player is, the character as one record. Every "do not copy" of the chapter's §7 is followed from the start (§8). The repository already has the seam: the client draws a snapshot and never reads `World` |
| 12 | Data model and content census | The proposal is adopted | The chapter's eleven-point modern form, not the original's format: typed records, checked references, no load order, templates and inheritance. `Scene.txt` and `RecordFile` are the starting point. The census is the basis of §10 |

Cut outright, each named as affordable in `Blueprint.md` §9 or in its chapter's design notes: staged rooms, acted conversations, fragment-spliced speech, the offer tree, the cockpit view, a physics library. Also cut: the cloak device, which only NPCs carry. Cut by the owner's decision: a player ship designer. Not cut, although the blueprint lists them as cuttable: lanes as real travel, damage to parts of a ship, itineraries, empathy, approach flight when docking, discovery. Each of those is where this game's own systems plug in.

## 7. Systems the Frontier blueprint does not have

### 7.1 Voxel structure and damage

- **Materials.** Each palette family (ADR-002) is a material with a strength, a mass and a yield. Armour is a stronger outer material, not a divisor. This sits beside the emissive table in `GameCore/Palette`.
- **Modules.** A module is a named region of a model's voxels with a function: reactor, engines, weapons mount, sensors, hangar, refinery. Its health is the share of its voxels present. Below one threshold it works at a reduced rate, below a second it stops. The engine rule of the blueprint (ch. 02 §3.4: no nozzle left, no thrust) becomes the general rule.
- **The core.** Every ship and station has one core module. A ship whose core stops is destroyed and breaks up; this replaces "hull at zero". "Hull" on the HUD is the share of structural voxels present.
- **Separation.** Voxels that lose their connection to the core become a wreck: a drifting object that can be salvaged. This replaces shed parts and dropped equipment.
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
| Wing | Orders it, from either view | Escorts in formation on the own ship. They dock, ride lanes and jump with it |
| Utility ships | Assigns an itinerary once | Tugs and miners run it unattended: claim to outpost, outpost to a base and back |
| Garrison | Sets a defend directive | Stays at the outpost |

Every owned ship is a blueprint NPC ship definition (ch. 06 §3.6): an archetype, a loadout and a pilot, with the player as owner. It is built in the yard from a hull the player has bought the plan for, and refitted there under the ordinary hardpoint rules. Crew capacity caps the fleet. The numbers stay small on purpose: a wing of about three, a few utility ships.

## 8. Rules that keep multiplayer possible

Single player ships first. These constraints apply from the first slice, because the Frontier blueprint is explicit that the split costs little early and a great deal late. Constraints 1 to 7 and 9 shape code and are written in AGENTS.md as R18 to R24; 8 and 10 are game behaviour and stay here.

1. **One message catalogue.** Presentation and authority exchange messages and never call each other. The in-process transport and a network transport carry the same messages. `GameApp` does not reference `GameLogic`; that edge is already forbidden.
2. **The authority decides.** Damage, purchases, builds, mining yields, docking, standing and discovery are computed by the authority from commands. The original's trust in the player's machine (ch. 11 §3.8) is not copied.
3. **The own ship is predicted, and bounded.** The presentation integrates the player's ship from input and is not corrected in normal play; the authority checks speed, fire rate and energy against the shared model.
4. **A fixed tick is the clock.** `TickClock` already provides it. No rule reads wall time.
5. **Masks change only by events from the authority,** applied with integer arithmetic so that both sides arrive at the same voxels. This is the one part of the game that must be deterministic (R16), and it needs an ADR before slice 3.
6. **No rule depends on pausing, on reloading or on seeing the whole system.** The view switch does not stop time; death has one rule for both modes; the tactical view shows sensor coverage only.
7. **State belongs to a character or to the world,** never to the process: locks, ambient switches, knowledge, the outpost and the fleet.
8. **What an absent player owns advances by ledger and cannot be attacked.**
9. **Content has stable text identifiers,** and both sides compare a hash of the simulation data when a session starts.
10. **A character is one self-contained record,** including the outpost's modules and masks and the fleet; content saves its own part.

## 9. How it lands in the repository

| Project | Takes on |
|---|---|
| `NeuronCore` | Voxel masks, the carving and connectivity arithmetic, typed records and reference checking |
| `NeuronServer` | The tick, sessions, the transport interface and the in-process transport, save infrastructure |
| `NeuronClient` | Rendering of voxels that change, the chase camera beside the orbit camera, devices to commands, audio |
| `GameCore` | What both sides share: archetypes, materials, the flight and equipment model, zones, the message catalogue |
| `GameLogic` | The authority: systems, world objects, regard, markets, the outpost and its ledger, and the content controllers for population, missions and the campaign |
| `GameApp` | The presentation: flight view, tactical view, HUD, base panels, the map |
| `AssetGenerator` | Recipes for families of hulls and modules, so that content volume is generated, not drawn |

Decisions this design forces, each an ADR before the slice that needs it: the deterministic mask and the damage event; the shared flight model on a fixed tick; the record schema that grows `Scene.txt` into system, base and archetype definitions; the message catalogue; rendering of changing voxels (today the surface is extracted once and the shadow map is drawn once); world scale. On scale: the sample's ship models, read as one metre per voxel, give escorts of 42 to 52 m and a frigate of about 190 m, which fits the blueprint's metric constants, so those can be carried over unchanged.

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
| 1. A ship that flies | An escort flown by cursor around the shipyard: throttle, strafe, thruster, engine kill, cruise, chase camera; the view toggle | 02 §3.7 to 3.10; 09 §3.1 to 3.5, 3.9; 11 | Top speed and turn rate come out of thrust and drag as ch. 02 predicts. In the tactical view the same ship obeys a go-to order |
| 2. A place to be | Dock at the shipyard and launch again; docked is the tactical view; one system with zones and a hazard; save on launch | 01 §3.1 to 3.5; 04 §3.1 to 3.5; 11 §3.10 | A full loop of launch, fly, dock and resume from a save |
| 3. A fight in voxels | Guns, shield, damage events, modules that fail, core death, wrecks; one opponent that attacks, evades and flees; the combat HUD | 03 §3.1 to 3.5; 05 §3.1 to 3.8; 09 §3.6 to 3.8; §7.1 | A duel has the rhythm of shield break and window. The ship panel's percentages are measured from masks. Carving holds the frame rate |
| 4. An outpost that grows | Sockets, build plans, Alloy, modules with services, power, crew and Water; repair | §3.2 to 3.5; §7.2 | "Finish hangar extension F-03" is completed by building it. A module shot off takes its service with it and can be rebuilt |
| 5. Mining and haulage | Carving rock into Ore; tractor; miner and tug itineraries; refinery; the ledger | 01 §3.6; 03 §3.9; 06 §3.8; §7.3 | The outpost sustains itself from the claim, present or away, at the same rate |
| 6. A wing | The yard builds escorts; formation; wing commands; tactical orders; raids | 05 §3.9 to 3.11; 06; §7.4 | "Defend the shipyard" can be won from the cockpit or from the tactical view, and "escort tug T-041 to dock" can be lost |
| 7. A living system | Factions and regard; population with patrols and traders that dock; cargo scans; text comms | 06 §3.2 to 3.8; 07 §3.1 to 3.6 | Shooting one faction's ship changes how two others treat the player and who raids the outpost |
| 8. An economy and a universe | Goods, markets, dealers, worth and level; several systems, jumps, lanes, routes, locks; the map and discovery | 07 §3.8 to 3.15; 01 §3.6 to 3.10; 04 §3.6 to 3.10 | A trade run, a fight and a shift at the outpost all raise worth. A route across three systems can be plotted and flown |
| 9. Jobs and a campaign | The trigger language, the job board, generated and scripted missions, checkpoints | 08 | An author builds an escort-and-ambush mission that ends at the outpost, from data alone |
| 10. Multiplayer | A network transport, observers and replication, accounts | 11 §3.6 to 3.13 | Two players meet, fight the same ships and see the same holes in them |

Slices 1 to 3 prove the feel and carry the largest technical risk. Nothing after them should start until a duel is good.

## 12. Where this design is weakest

1. **Scope.** The hybrid was the largest of the three options on offer. The design answers with the five identities of §3, the cut list of §6 and the budget of §10, but two views still mean two cameras, two interfaces and a pilot AI that has to serve as opponent, wingman and stand-in for the player. Slice 6 is the honest checkpoint: if the first six slices have not produced a game worth playing in one scene, the universe will not rescue it.
2. **Home against frontier.** The blueprint's difficulty curve is a journey outward: danger and profit grow with distance and the player rarely goes back. An outpost is an anchor. The further the player ranges, the longer the trip home; the more they stay home, the less the map matters. A small map, a central claim and lanes soften this and do not remove it. Open decision D3 is the real answer and it is not taken yet.
3. **Destruction as the damage model.** It has to give a readable fight with the blueprint's rhythm, hold the frame rate while surfaces change, and replay identically on two machines. The sample's renderer assumes a static scene. This is why slice 3 comes before any outpost work.
4. **Fly or command.** If a pilot flies the own ship as well as the player does, the flight half is optional; if the tactical view is never worth the lost seconds, the command half is. The balance is one tuning dial and will take more than one try.
5. **An unbounded faucet.** The blueprint's economy has fixed prices and unlimited stock, and stays bounded because a hold is small and a trip takes time. Mining and refining add a source that runs while the player does something else. Haulage throughput has to be the limit, and the ledger must never pay better than being present.
6. **Leaving as a tactic.** If raids happen only around a present player, a player in trouble can jump out to stop one. Constraint 8 needs a companion rule for a raid already in progress.
7. **What the blueprint does not know.** Rendering, effects and audio are requirements only (ch. 09 §3.10 to 3.14), and campaign failure was never recovered (ch. 08). Those gaps are inherited.

## 13. Open decisions

Each is the owner's. The recommendation is what the rest of this document assumes.

| # | Decision | Recommendation |
|---|---|---|
| D1 | What death costs | One rule for both modes, the blueprint's multiplayer rule (ch. 11 §3.12): back to the last base with the ship as it was fitted there, cargo lost. Reloading a save then stays a convenience and is not a rule |
| D2 | Can the outpost be lost | No. The command block is the blueprint's indestructible solar: everything attached to it can be shot off, the claim itself survives |
| D3 | One claim or several | One in the first release. Forward claims further out as campaign rewards, to answer weakness 2 |
| D4 | What sizes a raid | The place and the owner's standings, as for all population. Not the outpost's worth: the blueprint keeps danger a property of the map |
| D5 | The HUD's DESIGN tab | Rename it YARD: build and refit ships from bought hull plans. With no ship designer it has nothing to design |
| D6 | Which ships the player may fly | Fighters and freighters. Frigates are owned and commanded, not piloted: cursor flight is tuned for hulls that turn at 23 to 96 degrees a second |
| D7 | Can the outpost sell to visiting traffic | No. Surplus is hauled to a base, by the player or a tug. Profit stays a matter of knowing the map |
| D8 | The setting | Not part of this document. The sample's names (Kessler Reach, Shipyard Command, F-317) are working titles |
| D9 | Campaign in the first release | A short one (§10), since worth gates and locked jump points are what pace the open game |
| D10 | Where the tactical view is available | Anywhere, limited to owned sensors |
| D11 | Voice | None at first. Comms are text; the rules of chatter (ch. 06 §3.12) are kept |
| D12 | Wing size | About three, capped by crew |

## 14. Basis

Read in full for this document: `Blueprint.md` and blueprint chapters 00, 01, 02, 03, 07 and 11. Read for purpose, structure and design notes, with selected sections: chapters 04, 05, 06, 08, 09, 10 and 12. From this repository: `README.md`, `AGENTS.md`, ADR-001 to ADR-003, `Assets/Scene.txt`, `GameLogic/World.h`, `GameCore/WorldSnapshot.h`, `GameCore/Palette.h` and the reference screenshot. Model sizes in §9 were read from the `SIZE` chunks of the committed `.vox` files.

Nothing here was built or run. Every number in §10 is a proposal, and every blueprint number quoted is the blueprint's own.
