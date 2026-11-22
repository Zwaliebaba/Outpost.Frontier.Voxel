# ADR-007 — The simulation tick, time and position

**Status:** accepted · **Date:** 2026-11-22

## Context

The sample ticks its server at 20 Hz, and the only rule it runs is resource income. The design adds cursor flight with response times down to 0.03 s (the Frontier blueprint's fighter roll, GameDesign.md §11 slice 1), server-decided combat (R19), and a scale target of 100 players in systems 80 to 100 km across (§8.2). AGENTS.md R20 already makes the tick the only clock a rule may read. Three things were open: the rate, how wall time becomes ticks exactly, and how positions are held so that a system's rim is as precise as its centre.

Two facts about the sample shaped the decision:

- `TickClock` kept whole microseconds per tick. At 20 Hz a tick is exactly 50,000 µs. At 60 Hz it would be 16,666 µs after truncation, so the clock would run fast by 40 µs a second: 36,001 ticks in ten minutes instead of 36,000, by arithmetic.
- The smoke test already advances one tick per rendered frame at 1/60 s, so at 20 Hz its world ran three times too fast. At 60 Hz the two agree.

## Decision

### Rate

**60 ticks a second, on both halves.** The blueprint's server ran at 30 Hz with a variable step. At 60 Hz the fastest response the blueprint authors is longer than one tick, cursor flight needs no extra smoothing in single player, and integrating even the 600 ships of the 100-player load is cheap. The rate does not set bandwidth: in multiplayer, state is sent at 20 to 30 Hz through the observer (§8.2, R25).

### Wall time to ticks

- `TickClock` keeps the elapsed microseconds it has not yet spent, multiplied by `TICKS_PER_SECOND`. A tick is due for every `MICROSECONDS_PER_SECOND` of that sum, and the remainder carries over. The count is therefore exact at any rate, with nothing truncated.
- After a stall, at most `MAX_TICKS_PER_ADVANCE` ticks are run, a quarter of a second (15 at 60 Hz), and the rest of the stall is dropped. At 20 Hz the cap was 5, the same quarter second.
- Wall time is read in one place, the executable's main loop, and turned into ticks there (R20). `Interpolation()` gives the presentation the fraction of the next tick for drawing between states; no rule reads it.

### Position

These parts arrive with the code that first needs them: world objects in plan task F4, moving ships in slice 1.

- **World positions are `double`,** on both halves. That covers everything a system's coordinates locate: ships, solars, zones, munitions and the camera.
- **Smaller quantities are `float`:** velocities, orientations, offsets inside a model, and anything relative to an object.
- **Rendering is camera-relative.** Each instance's transform is built from its position minus the camera's, subtracted in `double` and then converted to `float`. The GPU never sees a coordinate the size of a system.
- **On the wire** (slice 10), positions are quantized relative to a coarse cell, not sent as raw doubles.

## Consequences

- Rules count durations in ticks of 1/60 s. A rate change is a change to `TICKS_PER_SECOND` and to whatever tests pin the rate. `TickClockTests` asserts 60 with a `static_assert`, so such a change cannot happen silently.
- `MICROSECONDS_PER_TICK` is gone. A tick is not a whole number of microseconds at 60 Hz, and code that needs a tick's length in seconds computes it from `TICKS_PER_SECOND`.
- Income per minute stays exact: `World` spreads it over `TICKS_PER_SECOND × 60` ticks with the remainder carried, which no rate changes.
- A `float` at 100 km resolves to about 8 mm (2⁻⁷ m), which is visible as jitter in motion and shading. Holding positions in `double` keeps a ship at the rim of a system as smooth as one at the centre. The cost is 24 bytes per position instead of 12, on a few thousand objects.

## Verified

- `TickClockTests` (five tests: whole ticks, carried remainder, ten minutes exactly 36,000 ticks, stall cap, interpolation) compiled with g++ 13.3.0 at `-std=c++23 -Wall -Wextra -Werror -Wconversion -Wsign-conversion` against a stand-in for the unit-test header, and all five passed. That is not the MSVC build: CI's Debug|x64 build and vstest run are the gate.
- `WorldTests` were read for rate assumptions. The income tests use `TICKS_PER_SECOND × 60` and still hold. The capacity test runs 40 ticks at 1,200 per minute, which is 13 at 60 Hz, still over the cap of 100.
