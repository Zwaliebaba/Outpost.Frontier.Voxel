# AGENTS.md — Engineering Rules for *Outpost:Frontier*

Operating instructions for every agent (and human) writing code in this repository. **Read this before generating a single line.**

*Outpost:Frontier* is a greenfield C++23 game and a hobby project with one developer: a Direct3D 12 game for Windows, built with MSVC and MSBuild. This file is about **how code is written here** — naming, layout, build settings and the standing rules of the codebase. It is not the design: what the game *is* belongs in [`Design/GameDesign.md`](Design/GameDesign.md).

**The tree is young.** It holds the first runnable sample — `Outpost.Frontier.slnx`, the projects in §2 and the editable voxel models — and that code was written to these rules from its first line. Nothing below is a target to migrate towards; it describes the code as it must be written. There is no legacy here and nothing is grandfathered, so a whole-tree run of any checker comes back clean, and a finding is a defect in the change that introduced it.

**Where these rules come from.** They are carried over from two sibling repositories: `Outpost.Warzone`, where the formatter and linter settings were measured against roughly 223,000 lines, and `Nomad-Commander`. That lineage is why `.clang-format` and `.clang-tidy` are what they are, and it is why code can move between the trees without a rename or a reflow pass. **What did not come across is the other trees' design, their decisions or their plan.** A decision taken there binds nothing here.

**What is authoritative, in order:**

1. **This file** — conformance: naming, style, build settings, and how to work here.
2. **`Design/ADR/`** — engineering decisions taken while building, one file per decision (§6). Numbering started at `ADR-001` in this repository and does not continue another's.
3. **The surrounding code** — for anything neither of the above covers, match the file you are editing.

**The design sits alongside rather than above.** [`Design/GameDesign.md`](Design/GameDesign.md) says what is built and this file says how. It is at concept depth: the pillars, the systems and where each comes from, a build order, and a table of decisions that are still open (its §13). Where it is silent, or the answer is one of those open decisions, a task that needs a design answer asks the owner and gets the answer written into that document before the code is.

**The design has one outside source, and it reaches this tree through that document only.** It adapts the concept blueprint in the sibling repository `Outpost.Frontier` (`Blueprint.md` and `blueprint/`), and for every system it marks as kept, the blueprint chapter is the specification until a chapter here supersedes it. That repository's code, its ADRs and its migration plan bind nothing here, the same as the two trees above.

If a rule here conflicts with a habit from another codebase, this file wins. If you think a rule is wrong or your task cannot be done without deviating, **say so in your report — never deviate silently.**

---

## 1. Naming convention (normative — no exceptions)

| Kind | Convention | Example |
|---|---|---|
| Type (class, struct, enum, concept, alias) | `PascalCase` | `SwapChainTarget` |
| Function, method | `PascalCase` | `PresentFrame()` |
| Member variable | `m_camelCase` | `m_deviceRemoved` |
| Static member (mutable) | `sm_camelCase` | `sm_activeDevice` |
| Global | `g_camelCase` | `g_instance`, `g_frameCount` |
| Parameter | `_camelCase` | `_fileName`, `_entityId` |
| Local | `camelCase` | `shadedColor` |
| Compile-time constant | `UPPER_CASE` | `WIDTH_PIXELS`, `CELL_PIXELS` |
| Enumerator | `PascalCase` | `DeviceLost`, `OutOfVideoMemory` |
| Macro | `UPPER_CASE` | `ENGINE_ASSERT` |
| Namespace | `PascalCase` | `Engine` |
| File | `PascalCase.cpp` / `.h` | `SwapChainTarget.cpp` |

**Note the split that catches people out: a `constexpr` is `UPPER_CASE`, an enumerator is `PascalCase`.** They are both compile-time and they are spelled differently on purpose — an enumerator is a *value of a type* and reads as one at the use site (`PageFault::OutOfVideoMemory`), while a constant is a number with a name and is meant to look like one. [`.clang-tidy`](.clang-tidy) enforces both, and it is the single source of truth for the option values; this document states the rules in prose and does not repeat the settings, so there is nothing to drift.

### The rules behind the table

**R1 — The leading underscore on parameters is deliberate.** It is legal C++: the reserved forms are `_Uppercase`, anything containing `__`, and `_lowercase` **at global scope**. A parameter is never at global scope, so `_fileName` is safe. Never introduce a reserved form — no `_Impl`, no `__helper`, no file-scope `_cache` (use `g_cache` in an anonymous namespace).

**R2 — A type name carries no prefix or affix, and that includes abstract ones.** An interface is `Transport`, not `ITransport`. A base class is not `BaseTransport` or `AbstractTransport`. PascalCase means the name and nothing else. This bans `CFoo`, `SFoo`, `EFoo`, `IFoo`, `FooBase`, `FooAbstract`, `FooImpl` and `_t` suffixes. Name the concept and let the concrete types say what they are:

```
Transport             ← the concept
├── UdpTransport      ← a socket-backed one
└── LoopbackTransport ← in-process, for tests
```

That tree is an illustration of the rule, not a description of anything. A base class for one derived class is ceremony: name the concept, and add the layer when a second thing needs it.

clang-tidy can require an *absent* prefix but cannot see a *present* suffix, so the repository checker carries the other half (§6).

**R3 — Compile-time constants are `UPPER_CASE`.** `constexpr`, `inline constexpr` and `static constexpr` members: `WIDTH_PIXELS`, `TICKS_PER_SECOND`, `CELL_PIXELS`. `sm_` is reserved for *mutable* statics, which are rare and must document their thread-safety.

**R4 — Acronyms capitalize as words**: `HlslSource`, `DxgiFactory`, `UdpTransport` — never `HLSLSource`. Identifiers from an external SDK keep that SDK's spelling (`ID3D12Device`, `DXGI_FORMAT`, `HRESULT`, `IDXGISwapChain4`) and are never renamed to fit.

**R5 — Template parameters are PascalCase**: `T`, `Fn`, `BlockBytes`, `Ts...`.

**R6 — Units belong in names; types do not.** `durationTicks`, `speedUnitsPerSecond`, `radiusMeters`, `volumePercent` are encouraged — a game measured in ticks, seconds, pixels and distances makes unit ambiguity a real defect class, and it is one the compiler cannot catch for you. Never encode the type: no `iCount`, `pEntity`, `strName`.

**R7 — A file is named for its primary type**, PascalCase, `.h` / `.cpp` only. `.hpp`, `.cc` and `.inl` are not used; template implementations live in the header. Exceptions, because MSBuild and the Visual Studio wizards spell them this way: `pch.h`, `pch.cpp`, `framework.h`, `targetver.h`, `Resource.h`.

**R8 — `m_` marks encapsulated state, not every field.** A `class` with invariants prefixes private members `m_`. A public aggregate — a `Desc` config struct, a wire record, a POD handed to the renderer — uses plain `camelCase` fields so brace initialization reads naturally.

**R9 — One namespace per layer, and the engine does not know the game.** Reusable engine code gets its own namespace; game code gets another. The split is a rule rather than a filing preference: if an engine type has to know a game concept by name in order to do its job, it is in the wrong layer. Test suites use `namespace <Project>Tests`.

**R10 — No `using namespace` at file scope in a header.** It leaks into every translation unit that includes it, and the failure it causes appears somewhere else. In a `.cpp` it is allowed for the unit-test framework and nothing else; otherwise qualify the name or write a local alias.

**R11 — One spelling per family, and it is the SDK's.** `color`, `initialize`, `serialize`, `normalize`, `quantize`, `synchronize`, `behavior`, `neighbor`, `center`, `gray`, `canceled`. Neither spelling is wrong English; the defect is a tree where a reader has to know which half they are in and a grep for one finds half the uses. `D3D12_CLEAR_VALUE::Color` settles which half wins. Prose is not checked — a design document may spell `flavour` and `harbour`; an identifier spells `flavor` and `harbor`.

### Worked example — this is the target style

```cpp
// Engine/SceneTarget.h
#pragma once

#include <cstdint>

namespace Engine
{

// R3: constant → UPPER_CASE. R6: the unit is in the name.
inline constexpr std::uint32_t SCREEN_WIDTH_PIXELS = 1920;
inline constexpr std::uint32_t SCREEN_HEIGHT_PIXELS = 1080;

// Enumerator → PascalCase, unlike the constants above.
enum class TargetFault : std::uint8_t
{
  DeviceRemoved,
  BadFormat,
  OutOfVideoMemory
};

/// The colour framebuffer the game draws into, and the depth buffer that goes with it.
/// R2: no prefix on the type. R8: private state carries m_.
class SceneTarget
{
public:
  struct Desc                                            // R8: aggregate → plain fields
  {
    std::uint32_t widthPixels;                           // R6: unit in the name
    std::uint32_t heightPixels;
    DXGI_FORMAT colorFormat;                             // R4: SDK spelling kept as-is
  };

  [[nodiscard]] static bool Create(ID3D12Device* _device,        // R1: _ on parameters
                                   const Desc& _desc,
                                   SceneTarget& _outTarget) noexcept;

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept { return m_widthPixels; }

private:
  ID3D12Resource* m_depthTarget = nullptr;
  std::uint32_t m_widthPixels = 0;
  bool m_deviceRemoved = false;
};

} // namespace Engine
```

### Enforcement

| Rule | Enforced by |
|---|---|
| The naming table, R1, R3, R5, R8 | [`.clang-tidy`](.clang-tidy), gated in CI over the whole tree |
| R2 affixes, R7 file names and project registration, R11 spellings, §2 flat directories, shader names and functional filters | `Build/CheckProjectFiles.py`, gated in CI |
| R4, R6, R9, R10 | Review. Check your own diff against the table before handing it back. |

**Both checkers run in CI** (§6). `Build/RunClangTidy.py` runs `.clang-tidy` over every hand-written translation unit with the switches its own project sets; `Build/CheckProjectFiles.py` carries the rules in its row, and also the Debug/Release alignment of §3 and R14's package table. `python Build/TestCheckers.py` proves that each checker fails on a planted violation of every rule it claims; a rule added to a checker comes with a test there.

---

## 2. Repository shape

The concrete layout is recorded in [ADR-001](Design/ADR/ADR-001-solution-layout.md); this is its summary. One solution, `Outpost.Frontier.slnx`, with solution folders that name the layer; the executable sits at the solution root:

| Folder | Project | Kind | Namespace | References |
|---|---|---|---|---|
| `Engine` | `NeuronCore` | static library | `Neuron` | — |
| `Engine` | `NeuronClient` | static library | `Neuron` | NeuronCore |
| `Engine` | `NeuronServer` | static library | `Neuron` | NeuronCore |
| `Game` | `GameCore` | static library | `Outpost` | NeuronCore |
| `Game` | `GameLogic` | static library | `Outpost` | NeuronCore, NeuronServer, GameCore |
| `Game` | `GameApp` | static library | `Outpost` | NeuronCore, NeuronClient, GameCore |
| *(root)* | `Outpost` | Windows executable | `Outpost` | all of the above |
| `Tests` | `NeuronCoreTests` | unit-test DLL | `NeuronCoreTests` | NeuronCore |
| `Tests` | `NeuronClientTests` | unit-test DLL | `NeuronClientTests` | NeuronCore, NeuronClient |
| `Tests` | `NeuronServerTests` | unit-test DLL | `NeuronServerTests` | NeuronServer |
| `Tests` | `GameCoreTests` | unit-test DLL | `GameCoreTests` | NeuronCore, GameCore |
| `Tests` | `GameLogicTests` | unit-test DLL | `GameLogicTests` | NeuronCore, NeuronServer, GameCore, GameLogic |
| `Tests` | `GameAppTests` | unit-test DLL | `GameAppTests` | NeuronCore, NeuronClient, GameCore, GameApp |

**Every project file is a standard Visual Studio project** ([ADR-005](Design/ADR/ADR-005-settings-in-project-files.md)): the layout the C++ project template writes, settings in the per-configuration groups the property pages edit, and nothing a property page cannot show — no shared property sheet, no custom targets, no comments. A setting is written only where it differs from the MSBuild default. The settings this file requires (§3, R16) are stated in every project and both configurations; a change to one is made in all of them in the same commit, and until `Build/CheckProjectFiles.py` compares them, that is review's job. Adding a project means creating it from the Visual Studio template, setting those properties, and adding it to the solution, to that table, and to `.clang-tidy`'s `HeaderFilterRegex`.

**Every library has its own test project, and every test project lives under `Tests\`**, as `Tests\<Library>Tests\<Library>Tests.vcxproj` ([ADR-006](Design/ADR/ADR-006-test-projects.md)). A suite tests the library it is named for, through that library's public headers; it references that library and what the library itself builds on, and nothing above it. A new library arrives with its test project, and that project ships with real tests rather than only the `SuiteSmoke` placeholder (§3). `Tests\` is a container folder, not a project: each test project inside it is flat like any other.

These are the standing constraints the layout satisfies, and any change to it has to keep satisfying.

**Project directories are flat, with exactly two sanctioned subdirectories.** C++ source — headers and `.cpp` alike — lives directly in its project's folder. **There is no `src/`, no `include/`**, and no other split of a project by file kind. This is not taste: `.clang-tidy`'s `HeaderFilterRegex` matches headers exactly one level in, so **a header in a subdirectory is silently unchecked** — no findings, no warning, and nobody notices for months. The two exceptions are the shader pipeline:

- **`<Lib>/Shaders/`** holds the HLSL, hand-written, named for the shader and its stage: `<Shader>VS.hlsl` for a vertex shader and `<Shader>PS.hlsl` for a pixel shader. Code shared between stages lives in `<Name>.hlsli` beside them and is never compiled on its own.
- **`<Lib>/CompiledShaders/`** holds what the compiler wrote: one header per `.hlsl`, `<Shader>VS.h` and `<Shader>PS.h`, each declaring a byte array `g_<Shader>VS` / `g_<Shader>PS`. It is **build output** — produced by an `FXCompile` item in the `.vcxproj` on every build, listed in `.gitignore`, skipped by every checker, and never edited or committed. The `.cpp` that binds the pipeline state includes it and nothing else does.

**Shaders are compiled into the executable.** The bytecode reaches the GPU from those generated byte arrays, and nothing else: no `.cso` beside the `.exe`, no shader loaded from disk at runtime, no `D3DCompile` and no `d3dcompiler_47.dll` dependency. A shader change is a rebuild.

**The edges run one way, and a layer never reaches sideways.** Engine code is built on by game code and never the reverse (R9), and two libraries at the same level share what is below them rather than each other. An edge that only exists "for now" is an edge, and it is the one that will be impossible to remove later.

**The project files are part of the source.** Adding, removing or moving a file means editing the owning `.vcxproj` **and** its `.filters`. A file that compiles locally but is missing from the project fails only in CI — or worse, links a stale object nobody notices.

**Filters are functional.** A `.filters` file groups a project by what the code *does* — `Rendering`, `Audio`, `Input`, `Shader` — never by what kind of file it is. The Visual Studio defaults `Source Files`, `Header Files` and `Resource Files` are deleted when a project is created and never come back, and a `.h` sits in the same filter as its `.cpp`.

**There are no vendored SDKs; packages come from NuGet.** The build depends on the Windows SDK, the MSVC standard library and the packages R14 approves, restored from NuGet at pinned versions, and on nothing else.

**Build and IDE output is never committed** — `x64/`, `.vs/`, `*.user`, and anything a build step generates.

---

## 3. Build and verify

**x64 is the only platform.** No Win32/x86 configuration in any project or solution; do not add one, and do not write code that only works at 32 bits.

**The compiler settings are the settings.** Toolset `v145` (Visual Studio 2026), `/std:c++latest` (`LanguageStandard` `stdcpplatest`), `/permissive-`, `/W4` with **warnings as errors**, `/fp:precise`, `/arch:AVX2` (R16). There is no CMake. If a build error tempts you to change the toolset, lower the language standard, turn off `/permissive-` or silence a warning — **stop and report instead.**

**Debug and Release are aligned by rule, not by luck.** Every setting that is not *about* optimisation reads identically in both configurations: language standard, conformance, warning level, include directories, precompiled header, floating-point model, instruction set. The two differ in exactly four things — `Optimization`, `_DEBUG` vs `NDEBUG`, `FunctionLevelLinking`/`IntrinsicFunctions`, and the linker's folding and LTCG switches. (MSBuild spells those four through a few more properties — `UseDebugLibraries`, `RuntimeLibrary` as the debug or release CRT, `LinkIncremental`, `WholeProgramOptimization`, `EnableCOMDATFolding`, `OptimizeReferences` — and that list is the whole of what may differ.)

That alignment matters more than it looks, because **CI builds Debug only** (§6). Release is compiled by whoever ships, and a Release that quietly lost an include directory or sat on an older language standard would not be discovered until then. A static check of the two configurations is what stands in for the build nobody runs.

**Build through the solution, never a `.vcxproj` directly.** Output paths and cross-project include directories are anchored on `$(SolutionDir)`, and MSBuild defines `SolutionDir` only for a solution build. Building a project file directly resolves every one of those paths against the *project* folder instead of the repository root. **It does not fail — that is the problem.** Output lands in the wrong folder, so the next solution build links against whichever copy is staler, and every cross-project include path becomes a directory that does not exist. The breakage is latent: it bites the first time a file reaches across projects, which may be weeks after someone got into the habit. To build one project, name it as a target on the solution — and with a `.slnx` that has solution folders, the target of a project inside a folder is the folder path, not the bare project name: `/t:Engine\NeuronClient`. A bare `/t:NeuronClient` fails with MSB4057. A project at the solution root is named bare: `/t:Outpost`.

```powershell
# Everything, from the repository root, naming the solution.
msbuild Outpost.Frontier.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo

# One project, still through the solution: <SolutionFolder>\<Project>, or the bare name at the root.
msbuild Outpost.Frontier.slnx "/t:Engine\NeuronClient" /p:Configuration=Debug /p:Platform=x64 /m /nologo
msbuild Outpost.Frontier.slnx /t:Outpost /p:Configuration=Debug /p:Platform=x64 /m /nologo

# Release, before you claim anything about it.
msbuild Outpost.Frontier.slnx /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
```

**A project does not put its own directory on the include path.** `cl.exe` already searches the directory of the including file first for a quoted include, so `#include "FileSys.h"` from a `.cpp` in the same folder resolves without help. Only the directories of *other* projects are listed, as `$(SolutionDir)<Project>`.

**Run the tests**, through `vstest.console.exe`, over every suite the build produced.

**vstest reports "no tests found" as a pass.** An empty suite is therefore worse than no suite: it is a green check mark over a library nobody exercised. Every test project ships a placeholder `SuiteSmoke` for exactly this reason; delete it when the first real test lands, never before.

**Run the checkers before you push.** They are seconds of Python and they are what CI runs:

```powershell
python Build\CheckFormat.py           # clang-format, whole tree. --fix rewrites the offenders
python Build\CheckProjectFiles.py     # build shape, project registration, R2/R7/R11
python Build\RunClangTidy.py          # needs a Developer PowerShell (INCLUDE must be set)
```

**A green build says nothing about whether the game draws.** For anything touching rendering, input, audio or presentation, launch the executable and look at it.

**Report what you actually did.** "Builds clean, not run" and "builds and runs" are different claims. Never imply the second when you only did the first, and say which configurations you built.

---

## 4. Layout and formatting

[`.clang-format`](.clang-format) is the authority for C++ layout: 2-space indent, 140 columns, Allman braces, pointer and reference bound left, includes never reordered. [`.editorconfig`](.editorconfig) covers everything clang-format does not — CRLF, UTF-8, final newline, trailing whitespace, and the non-C++ formats — and repeats the two numbers an editor needs before the first save.

**This tree is formatted, and CI keeps it that way.** A whole-tree format check here is a no-op. Format what you write; if the check fires, run `--fix` and commit the result rather than arguing with it.

- **Do not reformat what your task did not touch.** The check being green tree-wide means a drive-by reformat produces pure churn and buries your actual change.
- **Include order is load-bearing and grouped by hand**, which is why `SortIncludes` is `Never`: `pch.h`, then `<windows.h>` before any D3D12/DXGI/XAudio2 header, then the rest of the SDK, then project headers, then the standard library. A formatter reordering these behind a change's back is a correctness risk, not a style preference.
- **One header owns the Windows macro family, and nothing else defines any of it.** `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `NOMCX`, `NOSERVICE`, `NOHELP` are set in that one header, before `<windows.h>`, and the project files deliberately define none of them. Two owners of one macro is C4005, and `/WX` makes that fatal — `/D` spells a bare macro as `1` where a `#define` spells it as nothing, so the collision is guaranteed rather than possible. If you need `<windows.h>`, include that header; do not add the macros yourself.
- Do not silence a diagnostic with `#pragma warning(disable: ...)` to make a build pass. Fix the cause, or report it.

---

## 5. Rules for this codebase

**R12 — Graphics is Direct3D 12.** COM lifetimes are RAII from the first line — a raw `AddRef`/`Release` pair in new code is a defect, not a style. How the renderer is shaped — passes, render targets, resolution, scaling, window style, multisampling, text — is not settled here; it is a design and engineering decision taken when the renderer is built, and recorded as an ADR (§6).

**R13 is deliberately unused.** The rule numbers line up with `Nomad-Commander`, so that a rule cited by number means the same thing in both trees. R13 there is a rule about how that game ships, which is design, and it did not come across. Do not fill the gap; a new rule takes the next free number (R25 onward, below).

**R14 — Third-party libraries are approved one at a time, and arrive through NuGet.** The executable is built from the Windows SDK, the MSVC standard library and the libraries listed below, and nothing else. NuGet is the repository's package manager, by the owner's decision of 2026-11-22, and the only way a library enters the build: every package at an exact version pinned in the repository, restored by the build and by CI. No vcpkg, no vendored sources or binaries, no CMake build of someone else's code inside this solution. Updating a pinned version is a commit of its own.

**Being on NuGet does not make a library approved.** To add one, propose it in your report with what it buys and what it costs — do not add it. Once the owner approves, it joins the table below with an ADR, in the commit that first uses it. A package's imported build files are the one thing allowed into a project file beyond ADR-005's plain layout; ADR-017, the first package's ADR, records how.

| Library | Package | For | Bounds |
|---|---|---|---|
| MsQuic | `Microsoft.Native.Quic.MsQuic.Schannel` | The network transport ([`Design/GameDesign.md`](Design/GameDesign.md) §8.1) | Networked play only: single player and every test suite run on the in-process transport and never load `msquic.dll`. Its licence text ships with the DLL. Which project references it, and run-time loading or linking, are ADR-017's |

**It binds what the executable is built from, not what a development tool needs.** Scripts under `Build/` and `Tools/` never ship and never link, so a baker that needs Pillow does not reopen this rule. The same holds for test projects: they link the Microsoft C++ Unit Test Framework that ships with Visual Studio, and they are the only projects allowed to. **Third-party *content* is a different question and it is the owner's**: art, fonts and sound are allowed, and anything under a licence needs the owner's approval before it lands, with the licence text travelling with the bytes.

For Direct3D that list means what the Windows SDK installs: `d3d12.h`, `dxgi1_6.h`, `DirectXMath.h`, `wrl/client.h` (`Microsoft::WRL::ComPtr` is the COM smart pointer R12 asks for) and the `fxc`/`dxc` compilers that `FXCompile` drives. The companions a D3D12 sample reaches for by reflex — the DirectX Agility SDK and its `d3dx12.h`, DirectX-Headers, DirectXTK12, DirectXTex, and the DirectX Shader Compiler as a redistributable — are not on the approved list, so resource barriers and heap descriptions are written by hand. Each can now be proposed under the rule above; the Agility SDK is the likeliest, if GPU-driven culling at the design's scale (§8.2) is better served by newer D3D12 features than by the SDK's.

**R15 — Memory is plain C++.** `new`/`delete` where it must be, RAII everywhere, standard containers by default. No pool, slab or free-list allocator without a decision recorded in `Design/ADR/`.

**R16 — The floating-point model and instruction set are stated, not inherited.** Every project compiles `/fp:precise` and **`/arch:AVX2`**, stated explicitly in the project file rather than inherited from an MSVC default — a default is not a decision, and the symptom of losing one is two builds of the same code disagreeing about the same sum with no line to blame. Both settings are identical in Debug and Release.

**What `/arch:AVX2` costs is named rather than waved at.** It sets an AVX2 floor — Intel Haswell (2013) and AMD Excavator (2015); an older CPU meets an illegal instruction, not a message. And it lets MSVC contract `a*b+c` into an FMA even under `/fp:precise`, which changes float results, and may contract differently at different optimisation levels. Float code that must produce bit-identical results across builds cannot rely on it.

**If the game needs a deterministic core** — a simulation that replays, lockstep networking, a result that must reproduce from a seed — that is a decision recorded as an ADR, and inside that core: no `float` where a fixed-point or integer quantity will do (hold a fraction as integer hundredths and say so in the name, R6), no iteration over an unordered container whose order reaches the outcome, no wall-clock time (a tick is the clock, and wall time maps to ticks at one seam), and randomness from a pinned PRNG with a recorded seed — never `std::random_device`, never a hash of an address. The design names one such core, the voxel mask; R22 is its rule.

**R17 — A string you do not write is `const`.** `/permissive-` turns on `/Zc:strictStrings`: a literal is `const char[N]` and will not bind to `char*`. The fix is `const` on the signature, never a cast at the call site — a `const_cast` here is a lie about a literal that lives in a read-only section, and writing through it is a real crash rather than a theoretical one.

**R18 to R25 come from the design.** A design document does not only say what to build; some of what it says constrains how the code is *shaped*. [`Design/GameDesign.md`](Design/GameDesign.md) §8 lists what keeps multiplayer possible in a game that ships single-player first, and §8.2 what keeps it possible at 100 players in one scene; the constraints in them that shape code are the eight rules below. They bind from the first line of game code, because the split they protect costs little now and a great deal to retrofit. The design says *authority* and *presentation*; in this tree those are the server half (`GameLogic` on `NeuronServer`) and the client half (`GameApp` on `NeuronClient`). The constraints that are game behaviour — what an absent player's outpost does, what a saved character holds — stay in the design. All eight are review's to enforce.

**R18 — Client and server exchange data and never call each other.** `GameApp` does not reference `GameLogic` and `GameLogic` does not reference `GameApp` (§2); what crosses between them is a type defined in `GameCore`, handed over by the executable. Today that is `WorldSnapshot`, passed by reference. When commands flow up and object state flows down, they are messages from one catalogue in `GameCore`, and the in-process hand-over and the MsQuic network transport carry the same ones. A fact one half needs from the other is a message, never a pointer.

**R19 — The server decides; the client asks.** Damage, purchases, construction, mining yields, docking, standing and discovery are computed in `GameLogic` from commands. `GameApp` shows results and may predict them for display, but nothing it computes becomes true by being reported. The one exception the design grants is the motion of the player's own ship, which the client integrates from input and the server checks against the shared flight model.

**R20 — The tick is the clock.** Game rules advance in whole ticks of `Neuron::TickClock` and read no wall time: no `QueryPerformanceCounter`, no `std::chrono` clock and no frame time in `GameLogic`, or in the simulation code `GameCore` shares with the client. Wall time maps to ticks at one seam, in the executable. A duration in a rule is a count of ticks or a quantity per tick, and says so in its name (R6).

**R21 — Game code does not ask which mode it is in.** There is no "is this single player" test. Single player is the same server hosted in the player's process, so a rule that would differ between the modes is really a rule about something else — who owns an object, whether its owner is present — and is written against that. For the same reason no rule depends on pausing, on reloading a save, or on the client seeing a whole system.

**R22 — A voxel mask changes only by data the server issued: an event applied in integer arithmetic, or an exact snapshot or brick diff.** Construction, damage, repair and mining all change which of a model's voxels are present (the design's §3.3), and client and server must arrive at the same voxels from the same event. That makes the mask the deterministic core R16 describes, and R16's conditions hold inside it: no `float` in deciding which voxels go, no unordered iteration that reaches the result, randomness only from a seed the event carries. The code that applies an event is one function in one library, used by both halves. A client that joins late or comes into range is brought up to date by a snapshot or a diff of dirty bricks that names the last event it includes, never by guessing (§8.2); events then continue from that one. The ADR that fixes the event and the arithmetic is due before the first code that carves a voxel.

**R23 — Game state belongs to a character or to the world, never to the process.** No game state lives in a global, a static or a singleton: not a lock, not a switch a mission set, not what a player has discovered or owns. Each is a field of a character's record or of the world's, so that a second player is a second record and not a rewrite. `g_` and `sm_` remain for what is truly per-process, such as the device.

**R24 — Content is named by a stable text identifier.** A record refers to another record, a model, a socket or a text by name, and every reference is resolved and checked when content loads; an unknown key or a dangling name is an error with the file and line, as it already is for `Scene.txt`. Nothing persistent and nothing on the wire identifies content by load order or by an index into a table.

**R25 — The server sends world state only through an observer.** Every client session is an observer (the design's §8.2): the server decides per observer which objects it knows, in what order of priority and within what byte budget, and encodes those messages from the world without changing it. Single player is one observer on the same path, which passes every object; interest, priority and budgets are added behind it with multiplayer, without changing its callers. No message carries the whole world, and nothing in the client may assume it has been told about every object in a system.

**R26 and up are reserved.** A new rule takes the next free number, without renumbering anything above. A rule with a design source cites the section it comes from. Do not invent one without a source and do not import one from another tree: a rule with no source behind it is a rule nobody can settle an argument with.

---

## 6. Working rules

**Stay in scope.** Do what the task asks. Adjacent code that offends you is not part of the task — note it in your report and move on. Unrequested "while I was in there" changes are the main way a young tree acquires regressions it cannot bisect.

**Record decisions as ADRs.** An engineering decision — a file format, a wire protocol, a subsystem's shape, an exception to a rule here — goes in `Design/ADR/` as one file per decision, numbered in order from `ADR-001-<slug>.md`, stating the context, the decision and what it forecloses, in the same commit as the change that implements it. Figures in an ADR are measured, not estimated — if you quote one, say how you measured it. A decision nobody wrote down gets re-litigated every few months by whoever forgot it. The design lists the decisions it forces and the slice each is due before ([`Design/GameDesign.md`](Design/GameDesign.md) §9); those are ADRs like any other.

**The checkers are part of the tree.** `Build/CheckFormat.py`, `Build/CheckProjectFiles.py` and `Build/RunClangTidy.py` are what §1, §2 and §3 lean on, and `Build/TestCheckers.py` tests them. A checker that misses a violation is a defect in the checker: fix it, and add the case to the self-test.

**What CI runs.** [`.github/workflows/build.yml`](.github/workflows/build.yml) has two jobs: a Windows job that checks the build shape, builds **Debug|x64**, runs the test suites and then clang-tidy; and a Linux job that self-tests the checkers and checks formatting on a pinned clang-format. **Every step that has something to run blocks; a step whose input does not exist yet is skipped, not faked.** Each gate is guarded on the file it needs — the checker script, the solution, the built test DLLs — so the workflow is honest about today's empty tree and starts gating the moment that file lands. The guards are the only concession: nothing is `continue-on-error`, and a script that exists and fails still fails the build. Remove a guard once its input is permanently there, not before, and never add one to get past a red build.

**CI does not build Release.** The Windows build is the slow half of the pipeline and a second configuration roughly doubles it for a tree where the two differ only in optimisation. What stands in for it is the static alignment check on the two configurations (§3) — and, before a release, an actual `Configuration=Release` build by whoever is shipping. If you change something that could plausibly break only under optimisation, build Release yourself and say so.

**Commits and PRs.** Branch off `main`; small, focused commits with an imperative subject describing the change, not the process. One change per PR. CI must be green. Never commit build output, `.vs/` or `.user` files.

---

## 7. Before you hand work back

- [ ] Naming conforms to §1 — `_` on parameters, `m_` on class state, `UPPER_CASE` constants, `PascalCase` enumerators, no `I`/`C`/`Base` affixes.
- [ ] Only the lines the task required were changed; no reformatting, no drive-by fixes.
- [ ] New, removed or moved files are in the `.vcxproj` **and** the `.filters` of every project involved.
- [ ] No project's `ConformanceMode`, `LanguageStandard`, `WarningLevel` or `TreatWarningAsError` was changed, and no warning was silenced with a pragma.
- [ ] Debug and Release still agree on everything §3 says they must.
- [ ] No library outside R14's approved table, every package at a pinned version, each within its bounds.
- [ ] The checkers pass: `CheckProjectFiles.py`, `CheckFormat.py`, `RunClangTidy.py` (CI runs the last if you cannot), and `TestCheckers.py` if you changed a checker.
- [ ] It builds Debug|x64, and every test suite runs and passes.
- [ ] If it touches rendering, input, audio or presentation: it was **run**, not just built.
- [ ] `Design/ADR/` has a new file if the change *was* a decision.
- [ ] If it touches game rules: R18–R25 hold — only `GameCore` data crosses between client and server, the server decides, no wall time, no mode test, no game state in a global, state goes out only through an observer.
- [ ] If the task needed a design answer: it is written in `Design/GameDesign.md`, not only in the code.
- [ ] Your report states plainly what you verified, what you assumed, and any rule here you had to bend.
