# ADR-024 — Assets deployed by a post-build copy

*Decided by the owner on 2026-11-22. Supersedes the asset deployment of ADR-001 and the asset line of ADR-005.*

## Context

ADR-001 deployed `Assets/` by listing every asset file in `Outpost.vcxproj` as a Visual Studio *Copy file* item (`CopyFileToFolders`), with a destination per configuration and an `Assets\…` filter in the `.filters` file. ADR-005 kept that when the property sheet went. By slice 2 that was 42 files: 42 items with two destinations each in the project, 42 more in the filters, and six asset filters. Every new model, content file or atlas meant three edits, and `Tools/ValidateStation.py` checked that every model had been given them.

The owner asked for the items to be replaced by a post-build action that copies all of `Assets\` to the output directory.

## Decision

- **`Outpost.vcxproj` has one post-build event in each configuration**, identical in Debug and Release (AGENTS.md §3):

  ```
  robocopy "$(SolutionDir)Assets" "$(OutDir)Assets" /MIR /NJH /NJS /NFL /NDL /NP
  if %ERRORLEVEL% GEQ 8 exit /b %ERRORLEVEL%
  exit /b 0
  ```

  It is the *Build Events › Post-Build Event* property page, so the project stays one that the property pages describe in full (ADR-005).
- **`/MIR` mirrors the folder.** A changed file is copied, a new one is added, and a file removed from `Assets\` is removed from the output too, so a deleted model cannot linger beside the executable and be loaded by mistake. The output's `Assets\` belongs to the build alone.
- **Robocopy reports success with exit codes 0 to 7** (nothing copied, files copied, extra files removed, and their combinations) and failure with 8 or more. The event passes on a failure and reports success otherwise, because MSBuild would read any non-zero code as a failed build.
- **Asset files are not project items.** A new asset is deployed by putting it under `Assets\`; nothing in the project names it. The executable still finds its assets by walking up from its own directory (ADR-001), and test DLLs still find `Assets/` above them.
- **`Tools/ValidateStation.py` checks the event instead of the items:** both configurations must have a post-build event that copies `$(SolutionDir)Assets` to `$(OutDir)Assets`.

## Verified

Measured on the development machine, Debug|x64, from the command line:

- With `x64\Debug\Assets` deleted, a solution build recreated it with all 42 files and 14,191,541 bytes, and `diff -rq` found it identical to `Assets\`.
- With only the time stamp of `Assets\Content\System.txt` changed and no source changed, the next solution build ran the event and copied the file: MSBuild runs a C++ project's post-build event on every build of the project.
- A file put in `x64\Debug\Assets` by hand was gone after the next build.

## What this forecloses, and what it costs

- **Visual Studio's fast up-to-date check no longer sees asset edits.** The IDE skips a project it judges up to date, and asset files are no longer inputs it knows of. A change to `Scene.txt` alone is therefore not deployed by F5 or *Build* in the IDE until something else makes `Outpost` build, or a *Rebuild*. Command-line builds and CI are not affected. This is the convenience ADR-001 bought with the items, and it was not measured in the IDE.
- **Assets no longer appear in Solution Explorer** under the `Outpost` project. They are files in the repository, edited from there.
- **Everything under `Assets\` ships.** There is no per-file choice any more: a file that should not be beside the executable does not belong under `Assets\`.
