# ADR-017 — NuGet packages and the network transport over MsQuic

**Status:** accepted · **Date:** 2026-11-22

## Context

The owner decided on 2026-11-22 that NuGet is the repository's package manager and that networked play goes over QUIC with MsQuic's Schannel build, from the NuGet package `Microsoft.Native.Quic.MsQuic.Schannel` at an exact pinned version (AGENTS.md R14; GameDesign.md §8.1). Three things were left to this ADR:

- how a package enters a project file that ADR-005 otherwise keeps plain;
- where MsQuic is referenced, and whether `msquic.dll` is linked or loaded;
- what the transport does with streams, datagrams, certificates and MsQuic's threads.

R14 bounds all three: single player and every test suite run on the in-process transport and never load `msquic.dll`.

The MVP plan's task F7 asks for a spike before slice 2 grows the catalogue. It should send the real catalogue between two endpoints on localhost and show four things:

- every kind crosses in its delivery class;
- state fits the datagram budget;
- under loss, state stays current and the stream stays ordered;
- a changed host certificate is refused.

The spike is `QuicSpike`, a console tool in the `Tools` solution folder. Its results are below.

## Decision

### How a package is referenced and restored

- **`packages.config`, the native NuGet format.** It sits beside the project that uses the package and pins each one with an exact `version` (`2.6.4` for MsQuic). `PackageReference` is not used, because NuGet does not support it for native C++ projects.
- **The package's own build file is imported, and only that.** The import is one `<Import>` of the package's `build\native\*.targets`, in the template's `ImportGroup Label="ExtensionTargets"`. It carries the `Condition="Exists(...)"` Visual Studio writes, so that a project still loads before a restore. Visual Studio also writes an `EnsureNuGetPackageBuildImports` target; that target is left out, because ADR-005 allows no custom target. Without a restore, the build therefore fails at the first `#include <msquic.h>` rather than with NuGet's message. The project also lists `packages.config` as a `None` item, as Visual Studio does.
- **This is ADR-005's one exception**, and `Build/CheckProjectFiles.py` enforces its bounds. A project may import the template's files and nothing else, except a file under `packages\<id>.<version>\`. That `<id>` must be on R14's table, and `packages.config` must pin an exact version.
- **Restore.** Packages go to `packages\` at the repository root, which `.gitignore` excludes. Visual Studio restores on build. Elsewhere, a restore comes first: `msbuild Outpost.Frontier.slnx /t:restore /p:RestorePackagesConfig=true` (AGENTS.md §3). CI runs this as a step of its own before the build, so that a feed problem reads as one.
- **Updating a pin** is a commit of its own. It changes the `version` in `packages.config` and the version in the import path together.
- **clang-tidy reads the package's include directory from the package's build file.** It does so by expanding `$(MSBuildThisFileDirectory)`, so `Build/RunClangTidy.py` sees what `cl.exe` sees. It refuses to run if a package is not restored. `Build/TestCheckers.py` covers both behaviours.

### Where MsQuic is referenced, and how it is loaded

- **Today only `QuicSpike` references the package.** Nothing in the game or its test suites does.
- **`msquic.dll` is loaded at run time, never linked.**
  - `LoadLibraryExW(L"msquic.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR)` loads it from the executable's own folder, never from the working directory or `PATH`.
  - `GetProcAddress` resolves `MsQuicOpenVersion` and `MsQuicClose`, and the code asks for `QUIC_API_VERSION_2`.
  - The package's build file still adds `msquic.lib` to the link, but no code names an export, so the linker writes no import.
  - Measured with `dumpbin /dependents`: none of `QuicSpike.exe`, `Outpost.exe`, `AssetGenerator.exe` or the six test DLLs depends on `msquic.dll`.
- **The DLL sits beside the game anyway.** The package's build file copies `msquic.dll` into the shared output folder `x64\<Configuration>\`, next to `Outpost.exe`. That copy is harmless, because a process loads the DLL only by asking for it.
- **In slice 10 the transport keeps this shape.**
  - One engine library holds the code that includes `msquic.h` and loads the DLL. It is either `NeuronCore` or a new engine library that both halves reference (GameDesign.md §11).
  - The executable takes the DLL into its output folder.
  - No test project imports the package.
  - Which library it is gets settled when that code is written, inside these bounds.
- **Licence.** MsQuic is MIT, and the package carries `LICENSE`. When the game first ships the DLL (slice 10), the licence text ships with it. The spike never ships.

### Streams and datagrams

- **A route's stream is a QUIC stream.** `STREAM_WORLD` is one unidirectional stream that the server opens once the handshake completes. The client allows one peer-opened unidirectional stream. A reliable message is written to the stream as it is encoded. Its header already states its payload length (ADR-008), so the receiver cuts the byte stream into messages with no further framing.
- **A datagram carries exactly one encoded message.** It is refused at send if it is larger than both the 1,100-byte budget and the maximum MsQuic reports for the connection.
- **The tick in each datagram message is its sequence number.** Every state message the catalogue has carries the tick it is from, and `ApplyMessage` keeps the copy with the newest tick per object or index. That is the "newest wins, older discarded" GameDesign.md §8.1 asks for, so no second counter is added. A per-datagram counter for acknowledgement and delta compression (§8.2) arrives with those, in slice 10.
- **State is built only when the connection can send.** The server builds a tick's state datagrams only once every datagram it handed MsQuic earlier has left MsQuic's send queue. It counts a datagram as gone at its first send-state change: sent, or canceled unsent. Otherwise it skips the tick; newest wins, so a state never built is one nobody misses.
  - This is §8.1's "datagrams are built when the connection can send", made concrete.
  - The measurements below show why it is required: without it, a congested connection queues stale state without limit and starves the reliable stream.

### Certificates

- **The host certificate is self-signed.** It is an RSA 2048 CNG key, signed SHA-256, with `CN=localhost` and the server-authentication key usage, and it is handed to Schannel as a certificate context.
- **The client pins on first connection (trust on first use).**
  - It connects with `QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION | QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED`, so there is no root to configure and the decision is its own.
  - The fingerprint is the SHA-256 of the certificate's encoding.
  - With nothing pinned, it pins and accepts. Otherwise it accepts only an equal fingerprint, and refuses anything else with TLS alert 42, `bad_certificate`. A refused connection ends with status `0x8041012A`.
- **The verdict is asynchronous.** The callback copies the fingerprint out, enqueues it and returns `QUIC_STATUS_PENDING`, because the certificate is valid only during the call. The draining thread decides and calls `ConnectionCertificateValidationComplete`.
- **Deferred to slice 10:**
  - where the host keeps its key between runs;
  - where a client keeps its pins, which is per-user client configuration and not game state (R23);
  - the warning shown when a pin fails;
  - account authentication over the encrypted stream.

### Threads

- **MsQuic calls back on its own worker threads.** A callback copies what it was given into an event, pushes it onto one mutex-guarded queue and returns. The thread that runs the tick drains that queue, and only that thread calls the API, opens streams, sends, closes handles or frees buffers.
- **The API forces three exceptions**, and each touches nothing the draining thread writes:
  - On a new connection, the callback must set the connection's handler and configuration before it returns. The configuration is fixed before the listener starts.
  - On a peer-started stream, the callback must set the stream's handler.
  - On a certificate, the callback must copy the fingerprint, since the certificate dies with the call.
- **Send buffers belong to MsQuic until it says otherwise.** Each send's bytes are handed over as its context, and reclaimed when the event that ends the send is drained: stream send complete, or a datagram's final state.

## Measurements

**How they were made.** `QuicSpike.exe` was run on Windows 11 Pro 10.0.26300 against MsQuic 2.6.4 (`2.6.4+163530150`, the newest stable release on nuget.org on 2026-11-22).

- **Build:** Debug. A shorter Release run, 300 ticks, also passed every check.
- **Load:** catalogue version 2 and 50 objects. For 600 ticks at 60 Hz (10 s), each tick sends the `Tick` message, an `ObjectState` for every object and two `Resource` datagrams. Every 30 ticks it also sends a `Ship` and an `Objective` on the stream. All 50 `ObjectSpawn` messages go on the stream at the start.
- **Loss:** induced by a UDP relay inside the spike. It drops packets in both directions from a seeded PCG32, so QUIC sees real loss: stream data is retransmitted and datagrams are gone.
- **Staleness:** sampled once a tick for every object. It is the server's tick minus the tick of the pose the client holds.
- **Variation:** the counts vary from run to run because of timing. Where several runs were made, the table gives each.

| Run | Ships on the stream | `ObjectState` datagrams arrived | Pose staleness, mean / worst (ticks) | Ticks withheld |
|---|---|---|---|---|
| Direct, first connection (pins the host) | 21 / 21 | 30,050 / 30,050 | 0.00 / 0 | 0 of 601 |
| Relay, 10 % loss each way, gated | 21 / 21 | 27,051 / 30,000 | 0.11 / 4; second run 0.13 / 5 | 1 and 3 of 601 |
| Relay, 10 % loss each way, not gated | 21 / 21 | 27,072 / 30,050 | 0.11 / 5; second run 0.13 / 7 | — |
| Relay, 30 % loss each way, gated | 21 / 21 | 10,728 / 15,400 | 6.72 / 49 | 293 of 601 |
| Relay, 30 % loss each way, not gated | 1 or 2 / 21, in four runs | 9,625 to 13,490 / 30,050 | 188 to 224 / 405 to 473 | — |
| Direct, host certificate changed | refused, nothing delivered | — | — | — |

Other figures, from every run:

- The largest datagram was 60 bytes, an `ObjectState`, against the 1,100-byte budget.
- MsQuic reported a maximum datagram of 1,196 bytes on the loopback path, both directly and through the relay.
- No datagram arrived by the stream or the other way round.
- Every message decoded, and none applied out of order.
- In every run that connected, both ends closed cleanly.

What the default run (`QuicSpike.exe` with no options: 10 % loss, gated) checks, and found:

- Every message kind crossed, each only in its delivery class.
- The stream delivered all 92 reliable messages byte for byte and in order, both directly and through the relay.
- Every state message fit the budget and the path.
- No pose was more than 30 ticks (0.5 s) behind the server. That threshold was set before the first measurement.
- The relay's loss reached QUIC: 2,469 packets were dropped, and MsQuic declared 3,112 datagrams lost.
- The changed certificate was refused.

The spike exits 0.

**What the 30 % runs say.**

- **Not gated, the connection collapses.** The server keeps queuing 53 datagrams a tick that the congested connection cannot send. MsQuic queues them rather than dropping the old, so the client receives ever older state, and the stream behind the queue stalls after its first round of facts.
- **Gated, the stream stays whole and ordered, and state is current on average.** But the worst object was 49 ticks behind, which misses the 0.5 s threshold. At 30 % loss each way, half of all round trips fail. Better than this needs §8.2's per-observer priority and budgets, not the transport.

## Consequences

- **A package is two lines in a project file and one in `packages.config`, and the checker holds the rest.** A second approved package follows the same path without a new ADR.
- **A clone needs a restore before a command-line build.** Visual Studio does it unasked, and CI does it as a step.
- **The transport's shape is proven against the real catalogue** while the catalogue is six kinds: reliable stream framing, one message per datagram, newest wins by tick, gating on the send queue, trust on first use, and callbacks that only enqueue. Slices 2 and 3 can add messages knowing their delivery classes work over a network.
- **Every future state sender needs the gate.** It is the transport's job to say when the connection can take more, and the server's to build nothing until then.

## What this forecloses

- `PackageReference`, vcpkg and vendored binaries, which R14 already rules out.
- Linking `msquic.dll`: a link would put the DLL in the import table of whatever links it, and R14 forbids the game loading it in single player.
- A certificate authority, or validating the host against Windows' root store. The client trusts what it pinned, and nothing else.
- Sending state faster than the connection drains it, and retransmitting state.
- More than one message in a datagram until slice 10. Packing many objects' state into one datagram is §8.2's per-observer budget, which also brings the per-datagram sequence number.
