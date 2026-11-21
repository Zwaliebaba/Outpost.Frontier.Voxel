# ADR-005 — Standard Visual Studio project files

**Status:** accepted · **Date:** 2026-11-21 · **Supersedes:** the `Build/Cpp.props` and `DeployAssets` parts of ADR-001

## Context

ADR-001 put the shared compiler, linker and shader-compiler settings in a property sheet, `Build/Cpp.props`, and deployed assets with a custom MSBuild target. The owner asked for project files that contain only standard Visual Studio settings: what the C++ project template and the property pages produce, readable and editable in Visual Studio without anything hidden from it.

## Decision

`Build/Cpp.props` is deleted, and every `.vcxproj` is written the way Visual Studio writes one:
- the template's layout and labels: `Globals`, `Configuration`, `ExtensionSettings`, `Shared`, per-configuration `PropertySheets`, `UserMacros`, per-configuration `ItemDefinitionGroup`s, then items and `ExtensionTargets`;
- no comments and no custom targets;
- settings only through property-page properties, and only where they differ from the MSBuild default.

Defaults are left out because the toolset already applies them: the output folder `$(SolutionDir)x64\$(Configuration)\`, the intermediate folder `x64\$(Configuration)\` inside each project, and optimisation, runtime library and Debug incremental linking derived from `UseDebugLibraries`. Also left out are no precompiled header, full paths, and the shader entry point `main`.

What is stated, in both configurations of every project:

| Property page | Setting | Why |
|---|---|---|
| C/C++ › General | Warning Level 4, Treat Warnings As Errors, SDL checks, Multi-processor Compilation | AGENTS.md §3 |
| C/C++ › Language | C++ Language Standard *Preview — latest*, Conformance mode Yes | AGENTS.md §3 |
| C/C++ › Code Generation | Floating Point Model *Precise*, Enhanced Instruction Set *AVX2* | R16 |
| C/C++ › Command Line | `/utf-8` | Sources are UTF-8, and no property page has this switch |
| C/C++ › Preprocessor | `_DEBUG` / `NDEBUG`, plus the template's `_LIB`, `_WINDOWS` or `_CONSOLE` | Template |
| Release only | Function-Level Linking, Intrinsic Functions, COMDAT folding, Optimize References, Whole Program Optimization, Incremental Linking off | Template; AGENTS.md §3 |
| HLSL Compiler | Shader Model 6.7, Debugging Information off, Treat Warnings As Errors, header output to `CompiledShaders\` as `g_<Name>`, no object file | AGENTS.md §2 |

Assets are *Copy file* items (`CopyFileToFolders`) in `Outpost.vcxproj`; see ADR-001.

## Verified

Both configurations were rebuilt from clean before and after, and the command lines MSBuild logged (`*.command.*.tlog`) were compared token by token. The only differences were the intended ones:
- object files moved to the default intermediate folder;
- the template's `_LIB`/`_WINDOWS`/`_CONSOLE` defines were added;
- `/Zc:__cplusplus` was removed, as nothing reads `__cplusplus`;
- `-HV 2021` was removed. HLSL 2021 is DXC's default, and the nine compiled shader headers are byte-identical without it.

After the change:
- 42 of 42 tests and the WARP smoke test pass in Debug and Release;
- all 20 asset files deploy, and an edited `Scene.txt` is redeployed by the next build;
- no `.cso` file is written.

## Consequences

- A required setting is stated in nine projects and two configurations each, so a change to one has to reach all of them, and nothing checks that yet. `Build/CheckProjectFiles.py` is meant to compare them; until it exists, that is review's job (AGENTS.md §2).
- A new asset file must be added to `Outpost.vcxproj` as a *Copy file* item, or it is not deployed. That is the same rule AGENTS.md §2 already applies to source files.
- In Debug, shaders compile without optimisation, which is Visual Studio's default for the HLSL compiler. AGENTS.md §3 allows Debug and Release to differ on optimisation.
