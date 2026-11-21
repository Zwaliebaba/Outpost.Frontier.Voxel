# ADR-006 — One test project per library, under `Tests\`

**Status:** accepted · **Date:** 2026-11-21 · **Supersedes:** the single `OutpostTests` project of ADR-001

## Context

The first sample had one test DLL, `OutpostTests`, in the repository root. It held every suite and referenced five of the six libraries. `NeuronServer` had one stray test inside the scene suite, and `GameApp` had none. The owner asked for all tests to live in a `Tests\` directory, with a test project for every library.

## Decision

- **Location:** each library `<Library>` has a test project `Tests\<Library>Tests\<Library>Tests.vcxproj`, in the solution's `Tests` folder, with namespace `<Library>Tests` (R9). `Tests\` only contains the projects; each project folder is flat, like any other project (AGENTS.md §2).
- **Scope:** a suite tests the library it is named for, through that library's public headers. It references that library and what the library builds on, and never a library above it. So `GameCoreTests` cannot reach `GameLogic`, and the income test moved from the scene suite to `GameLogicTests`.
- **Shared helpers:** each project keeps its own. `NeuronCoreTests` and `GameCoreTests` each have a `TestSupport.h` with only what their suites use. The other suites keep their one-line helpers in an anonymous namespace. No test-support library is shared between suites, because it would be a seventh library with nothing testing it.
- **Project files:** standard Visual Studio native unit-test projects (ADR-005), with the same required settings as every other project.
- **Discovery:** CI's test step already finds every `*Tests.vcxproj` in the tree and runs each DLL, failing if one was not built, so nothing in the workflow changed.

| Project | Tests | Moved from `OutpostTests` | New |
|---|---|---|---|
| NeuronCoreTests | 20 | VOX file, meshing and wireframe suites; axes and sockets (minus the scene check) | — |
| NeuronClientTests | 8 | Selection suite | — |
| NeuronServerTests | 4 | Tick-clock checks, split into four tests | Remainder carry, interpolation |
| GameCoreTests | 12 | Palette and scene suites; the shipped blueprint-dock check | — |
| GameLogicTests | 5 | Income per minute | First snapshot, fractional income, capacity, reload |
| GameAppTests | 6 | — | HUD stays on screen at 720p/1080p/4K with off-screen and behind-camera ships; build slots; HUD scaling; font sizes; draw constants (camera into voxel space, clip-space round trip) |

## Verified

- All six suites built with warnings as errors.
- In Debug and Release, `vstest.console.exe` over the six DLLs passed 55 of 55. That is the original 42 tests, with the stray tick-clock and income tests split and moved, plus 13 new ones.
- clang-tidy found nothing in the test sources, and `.clang-tidy`'s header filter matches the `Tests\<Name>Tests\*.h` headers.

## Consequences

- A new library needs a new test project; AGENTS.md §2 says so.
- `GameAppTests` builds a GDI font atlas, so it needs a Windows session with fonts; it does not need a GPU. The `windows-latest` CI runner provides both.
- The HUD vertex test pins the edge arrows and health bars to the screen. It deliberately leaves out a *selected* ship partly off screen, whose brackets may still leave the screen; that is the known cosmetic limitation in the README.
