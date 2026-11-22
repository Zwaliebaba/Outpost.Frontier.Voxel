# ADR-025 — Tool projects live under Tools\

*Decided by the owner on 2026-11-22. Amends the layout of ADR-001 and ADR-017.*

## Context

ADR-017 added `QuicSpike`, a console tool, in the `Tools` solution folder, but its directory sat at the repository root beside the libraries and the executable. The repository's other tools, the Python scripts that author, export and validate assets, already lived in `Tools/`. So the solution folder said one thing and the file system another, and the root mixed a throwaway spike with the projects the game is built from.

The owner asked for `QuicSpike` to move into `Tools/`, with every reference to it.

## Decision

- **A tool project lives in `Tools\<Tool>\<Tool>.vcxproj`**, beside the scripts in `Tools/`. `QuicSpike` is now `Tools/QuicSpike/`. `Tools\` is a container, like `Tests\` (ADR-006): it holds scripts and project folders, and each project folder inside it is flat (AGENTS.md §2).
- **Paths anchored on `$(SolutionDir)` are unchanged**: the include directories and the output in `x64\<Configuration>\`, so the tool is still `x64\Debug\QuicSpike.exe`. The paths relative to the project file go one level further up: its two project references and the MsQuic package import (`..\..\packages\…`, ADR-017).
- **The checkers needed no rule change.** `.clang-tidy`'s `HeaderFilterRegex` matches `QuicSpike/<Name>.h` at the end of a path, so headers under `Tools/QuicSpike/` are still checked. `Build/TestCheckers.py` names the project's new folder in the test that fakes the package restore.

## Verified

- A NuGet restore through the solution, then Debug and Release builds with no warnings. The build used the package from `packages/` at the repository root.
- `QuicSpike.exe` with no options passes every check, as in ADR-017.
- `CheckProjectFiles.py` is clean, `RunClangTidy.py` checks the moved files and is clean, and `TestCheckers.py` passes.

## What this forecloses

- A tool project at the repository root. The root holds the libraries, the executable, `Tests\` and `Tools\`.
