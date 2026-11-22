# ADR-001 — Solution layout, build shape and asset deployment

**Status:** accepted · **Date:** 2026-11-21

## Context

The repository started empty apart from AGENTS.md and its configuration files. The first runnable sample — a tactical view of a voxel shipyard, with its asset generator and tests — needs a solution, a project split that keeps the engine apart from the game (R9), a shader pipeline that compiles into the executable (§2), and a way for the executable to find its assets no matter where it is launched from.

Two choices departed from AGENTS.md as it stood, and the owner decided both before any code was written:

- **Shader directories are plural:** `<Lib>/Shaders/` and `<Lib>/CompiledShaders/`, not `Shader/` and `CompiledShader/`. AGENTS.md §2, `.gitignore` and this ADR say the same thing.
- **A `GameCore` library holds what the game's client, server and tools all need** — the palette, the scene definition and the world snapshot. Without it, either `GameApp` and `GameLogic` would reference each other, a sideways edge §2 forbids, or the tool would link the renderer.

## Decision

### Projects and edges

One solution, `Outpost.Frontier.slnx`, x64 only, Debug and Release:

| Folder | Project | Kind | Owns |
|---|---|---|---|
| Engine | `NeuronCore` | lib | Platform (`WindowsSdk.h`, file system), `.vox` I/O, voxel surfaces and wireframes, model axes, ray/box maths, the record-file parser, hashing and noise |
| Engine | `NeuronClient` | lib | The D3D12 device and swap chain, the renderer and its shaders, the orbit camera, screen projection, pointer gestures, the HUD batch and font atlas, frame capture |
| Engine | `NeuronServer` | lib | The fixed-rate tick clock |
| Game | `GameCore` | lib | The canonical palette and material table, the scene definition, the world snapshot |
| Game | `GameLogic` | lib | `World`, the authoritative server state, which ticks and publishes snapshots |
| Game | `GameApp` | lib | The client: scene assets, the tactical scene, the HUD, input |
| *(root)* | `Outpost` | exe | Window, message loop, command line, smoke test |
| Tools | `AssetGenerator` | console exe | Writes every model as a `.vox` file |
| Tests | `<Library>Tests` ×6 | test DLLs | One CppUnitTest suite per library, under `Tests\` (see ADR-006); no GPU |

Engine code is namespace `Neuron` and never names a game concept: the renderer draws voxels, lines, glows and HUD triangles, and the game decides what they are. Game code is namespace `Outpost`.

The edges run one way. `GameLogic` (server) and `GameApp` (client) share `GameCore` and never see each other. The client draws a `WorldSnapshot` and never reads `World`. `Outpost.exe` hosts both halves in one process for now; the snapshot is the seam where a network transport would go, but networking is out of scope. *Since the design ([GameDesign.md](../GameDesign.md)): networking is its slice 10, and AGENTS.md R18 makes this seam a rule from the first slice.*

### Build shape

- *Superseded by [ADR-005](ADR-005-settings-in-project-files.md): there is no shared property sheet; each project is a standard Visual Studio project.* Every project builds with toolset `v145`, `stdcpplatest`, `/permissive-`, `/W4 /WX`, `/fp:precise`, `/arch:AVX2` and `/utf-8`, into Visual Studio's default output folder `$(SolutionDir)x64\$(Configuration)\`. Debug and Release differ only where AGENTS.md §3 allows.
- **Shaders:** `<Lib>/Shaders/<Name>VS.hlsl` and `<Name>PS.hlsl` are `FXCompile` items. DXC from the Windows SDK compiles them to `/T vs_6_7` / `ps_6_7` (HLSL 2021, DXC's default), and writes `<Lib>/CompiledShaders/<Name>.h` holding the byte array `g_<Name>`. Shared code is in `.hlsli` files, which are not compiled alone. No shader PDB is embedded in either configuration; Debug compiles shaders without optimisation, which is Visual Studio's default. The headers are build output and ignored by git.
- **Single-project builds with a `.slnx`** name the solution folder: `msbuild Outpost.Frontier.slnx "/t:Engine\NeuronClient"`; a bare name for a project inside a folder fails with MSB4057. The executable sits at the solution root, so its target is `/t:Outpost`.

### Asset deployment

`Assets/` at the repository root is the single source: models, `Scene.txt`. Each asset file is listed in `Outpost.vcxproj` as a Visual Studio *Copy file* item (`CopyFileToFolders`, filter `Assets`) with `$(OutDir)Assets` as its destination. The build copies it when it changes, and Visual Studio's up-to-date check sees the edit, so F5 picks up a changed scene. A new asset file is added to the project the same way, like any source file. At startup the executable resolves assets from **its own module directory**, walking up until it finds `Assets/Scene.txt`. The working directory is never consulted. So the same binary runs under F5, from `x64\Debug\`, or from a copied folder; `--assets <dir>` overrides the search. The test DLL uses the same search from its own location.

### Toolchain, as built and verified

- Visual Studio 2026 Community 18, MSVC toolset `v145` (compiler 14.51), MSBuild 18.
- Windows SDK `10.0.26100.0` (`WindowsTargetPlatformVersion`), which also supplies `dxc.exe` and the D3D12/DXGI headers.
- Direct3D feature level 12_1 and shader model 6.7 are checked at device creation; a device without them fails with exit code 2.

## Consequences

- A new project has to be added to the solution, to the table in AGENTS.md §2, and to `.clang-tidy`'s `HeaderFilterRegex`, or its headers go unlinted.
- `NeuronClient` is the only project that links D3D12; `NeuronCore`, `GameCore` and the tool build and test without a GPU.
- Splitting `Outpost.exe` into separate client and server processes is a transport behind the snapshot, not a restructuring.
- Assets are copied, not linked: editing `Assets/Scene.txt` takes effect at the next build, or immediately with `--assets Assets`.
- *Added with the design:* what each project takes on as the game is built is listed in [GameDesign.md](../GameDesign.md) §9. The projects and the edges decided here do not change.
