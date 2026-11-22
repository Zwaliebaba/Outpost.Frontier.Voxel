# ADR-008 — Messages, their encoding, and the in-process transport

**Status:** accepted · **Date:** 2026-11-22

## Context

The server hands the client a `UniverseSnapshot` by reference, once a frame, in one process. AGENTS.md R18 says the two halves exchange data and never call each other; R19 says the server decides; R25 says world state leaves the server only through an observer. The design's §8.1 sends networked play over QUIC with MsQuic, with reliable streams for facts and datagrams for state. §8.2 needs damage ordered per object rather than globally, so that 100 players' traffic is not stalled behind one lost packet.

This ADR fixes what a message is, how it becomes bytes, how each kind travels, and what carries it while both halves share a process. It is plan task F3. Moving the snapshot onto this path is task F4, together with the observer that R25 requires.

## Decision

### The catalogue

- `GameCore/Messages.h` holds every message: a plain struct per kind, collected in one `std::variant`, with a `MessageKind` number each.
- `CATALOGUE_VERSION` is raised whenever a kind is added, removed or changes its encoding.
- The first four kinds carry what the snapshot carries today:
  - `Tick`: the server's tick;
  - `Ship`: identity and module health;
  - `Objective`: one objective, by index;
  - `Resource`: one resource, by index, stamped with its tick.
- Commands up (per-tick flight input, directives, requests) join with the slices that need them.

### Encoding

- **Encoded even in process.** Every message is turned into bytes before it is handed over, so only data can cross. A pointer or a reference cannot slip through by accident, and the network in slice 10 is a different carrier for the same bytes. The cost is an encode and a decode per message. That is negligible at the MVP's volume, but it has not been measured.
- **Header:** 8 bytes: catalogue version (16 bits), kind (16 bits), payload length (32 bits). Batching several messages under one header, which matters at §8.2's scale, is left to the replication ADR of slice 10.
- **Fields** (`NeuronCore/WireFormat`): little-endian, fixed widths, written with shifts so the host's byte order never matters. Floats and doubles go bit for bit; strings are a 16-bit byte count and UTF-8.
- **Decoding refuses, never guesses:** wrong version, unknown kind, fewer bytes than stated, more bytes than stated, or a payload that does not read as its kind, such as a flag byte that is neither 0 nor 1. Each refusal has its own `DecodeError`.

### Routes

Each kind has one fixed route, given by `RouteOf`: a delivery and, for reliable messages, a stream.

| Delivery | For | Today |
|---|---|---|
| Reliable, ordered within a stream | Facts that must all arrive, in order: identity, objectives, later commands and docking | `Ship`, `Objective`, `ObjectSpawn` on stream 0, the world stream |
| Reliable, sharded by object | Voxel damage events, which R22 needs complete and in order per object, not globally (slice 3) | — |
| Datagram, newest wins | State that changes every tick; the receiver keeps the copy with the latest tick | `Tick`, `Resource`, `ObjectState` |
| Datagram, redundant | Per-tick flight input, each datagram repeating the last few inputs (slice 1) | — |

A datagram message must fit `MAX_DATAGRAM_BYTES`, which is **1,100 bytes** of payload. QUIC guarantees that every path carries a UDP payload of at least 1,200 bytes. A datagram frame gives up part of that to the short header, the packet number, the 16-byte authentication tag and its own frame header, and 1,100 leaves room for all of them. In slice 10 the maximum MsQuic reports for each connection replaces the constant. A test encodes the largest datagram message of every kind against the budget.

### The in-process transport

- **Where it lives:** `NeuronCore/LoopbackTransport`, because both halves need it and neither may take it from the other's library (R18). It moves packets, never messages: the engine knows nothing of the catalogue (R9).
- **What it does:** delivers every packet in the order sent, datagrams included, single-threaded. It refuses a datagram over the budget, so a message too big for a network fails from the first slice, not at the first network game.
- **One concrete class:** there is no abstract `Transport` yet. A base class for one derived class is ceremony (R2); the interface is extracted when the MsQuic transport arrives as the second implementation, keeping `Send`, `Receive` and `Pending` as they are.

### The observer (plan task F4)

- The server encodes world state for each client through an observer, and nowhere else. In single player there is one observer, which passes every object.
- Interest, priority and byte budgets are added behind it in slice 10, without changing its callers (R25).

## Consequences

- Adding a message means a struct, a kind, a route, the encoding in both directions, a version bump, and a round-trip test.
- `ShipModuleState`, `ShipState`, `ObjectiveState` and `ResourceState` gain defaulted equality, so messages can be compared.
- A reader of an unknown or newer catalogue gets a clean refusal instead of misread bytes, which is what lets a host and a client of different builds fail politely (the blueprint's version check, ch. 11 §3.13).
- Until F4, nothing in the executable uses this path; the snapshot is still handed over by reference.

## Since: F4 put the executable on this path

Catalogue version 2 adds two kinds under the rules above, and the by-reference hand-over is gone.

- `ObjectSpawn` (reliable, world stream) introduces an object: its `ObjectId`, its content names (ADR-009), its owner, its scale and its pose. Later messages name it by id alone.
- `ObjectState` (datagram, newest wins) carries an object's pose as of a tick. A pose is a position in three doubles (ADR-007) and an orientation as a float quaternion: 40 bytes. With the tick and the id the payload is 52 bytes, and 60 with the header.
- `Ship` now names the object it is, and the receiver keys ships by that id rather than by name.
- `ApplyMessage` (`GameCore/Messages`) is the one function that turns a decoded message into the receiver's `UniverseSnapshot`. It drops newest-wins messages older than the copy it holds, and state for an object it was never told of.
- `GameLogic/Observer` is the observer of the decision above. It remembers what its client was told and sends only what changed: a spawn for an object the client has not seen, a pose when one moved, a ship, objective or resource when it differs. In single player it passes every object.
- The executable runs `Observer::Publish` and `TacticalClient::Receive` once a frame, after the ticks. A message that fails to encode, send or decode ends a smoke run with exit code 8 (`Protocol`).
- The client builds its scene from the objects it was told about, at the poses it received, and rebuilds when an object appears or moves. Until the first messages arrive it draws nothing. The first build after F4 asserted in `XMMatrixLookAtLH`, because an empty scene gave the shadow camera infinite bounds; the shadow camera now falls back to the origin when there is nothing to bound.
- **Measured:** the sample scene's first publish is 51 messages: one tick, 38 spawns, 4 ships, 3 objectives and 5 resources, as the smoke report prints. That report also says what the client was told. Drawing at a received pose rather than the scene's own matrix moved 5 % of the WARP capture's pixels by float rounding, and the smoke reference was stored again (`Design/Measurements/SmokeBaseline.md`, which records how that was traced).

## Verified

- Compiled with g++ 13.3.0 at `-std=c++23 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow`, together with the new suites (`WireFormatTests`, 5; `LoopbackTransportTests`, 3; `MessageTests`, 9), against a stand-in for the unit-test header. All 17 passed.
- clang-tidy 18.1.3 with the repository's `.clang-tidy` reported nothing on the new sources and tests. That run used the GNU driver, and needed `__cpp_concepts` raised by hand for libstdc++'s `std::expected` under clang 18. It is not CI's clang-tidy 22 through the MSVC driver, and the MSVC build and vstest run in CI are the gate.
- `Build/CheckProjectFiles.py` and `Build/CheckFormat.py` are clean with the new files registered.
