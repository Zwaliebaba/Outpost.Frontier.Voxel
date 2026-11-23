"""Runs clang-tidy over every hand-written translation unit in the solution (AGENTS.md §1, §6).

Each .cpp is linted with the switches its own project's Debug|x64 configuration gives MSVC,
through clang's MSVC driver (--driver-mode=cl): language standard, conformance, instruction set,
floating-point model, character set, runtime library, preprocessor definitions and the include
directories of other projects and of the NuGet packages it imports (ADR-017). The rules come from
/.clang-tidy, which clang-tidy finds itself.

Run it where MSVC's environment is set (a Developer PowerShell, or CI after "Import the MSVC
environment"): INCLUDE is what makes the CRT and the Windows SDK visible to clang. Run it after a
build, because NeuronClient includes the shader headers the build writes to CompiledShaders/, and
Tools/QuicSpike the headers of a package only a restore puts under packages/.

Usage:
  python Build/RunClangTidy.py                         every translation unit
  python Build/RunClangTidy.py GameLogic/Universe.cpp ... only these files
  python Build/RunClangTidy.py --dry-run               print the commands instead of running them
  python Build/RunClangTidy.py --jobs N --clang-tidy PATH --any-version

The version must be the one CI pins (CLANG_TIDY_VERSION in .github/workflows/build.yml):
  python -m pip install clang-tidy==<CLANG_TIDY_VERSION>
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))

import BuildTree  # noqa: E402

CONFIGURATION = "Debug|x64"  # what CI builds, and so what the linter must see
PACKAGE_IMPORT = re.compile(r"(?:^|[\\/])packages[\\/]", re.IGNORECASE)


@dataclass
class Unit:
    source: PurePosixPath
    project: BuildTree.Project
    arguments: list[str]


class ConfigurationError(Exception):
    pass


def package_include_directories(root: Path, project: BuildTree.Project, require_restore: bool) -> list[str]:
    """The include directories the build files of a project's NuGet packages add (R14, ADR-017)."""
    directories: list[str] = []
    for element in project.root.iter(f"{{{BuildTree.MSBUILD_NAMESPACE}}}Import"):
        target = element.get("Project", "")
        if not PACKAGE_IMPORT.search(target):
            continue
        path = Path(os.path.normpath(root / project.directory / target.replace("\\", os.sep)))
        if not path.is_file():
            if require_restore:
                raise ConfigurationError(f"{project.path}: {target} is not restored; restore the solution's NuGet packages first")
            continue
        imported = BuildTree.parse_xml(path.read_text(encoding="utf-8-sig"))
        for setting in imported.iter(f"{{{BuildTree.MSBUILD_NAMESPACE}}}AdditionalIncludeDirectories"):
            for directory in (setting.text or "").split(";"):
                directory = directory.strip()
                if not directory or directory.startswith("%("):
                    continue
                expanded = directory.replace("$(MSBuildThisFileDirectory)", str(path.parent) + os.sep)
                if "$(" in expanded:
                    raise ConfigurationError(f"{path}: cannot expand '{directory}'")
                directories.append(os.path.normpath(expanded))
    return directories


def project_arguments(root: Path, project: BuildTree.Project, require_restore: bool = True) -> list[str]:
    """The cl.exe switches a project's configuration implies, in clang-cl's spelling."""
    value = lambda name: project.property_value(CONFIGURATION, name)  # noqa: E731
    arguments = ["--driver-mode=cl", "/EHsc", "/nologo"]

    standards = {"stdcpplatest": "/std:c++latest", "stdcpp20": "/std:c++20", "stdcpp17": "/std:c++17"}
    standard = value("LanguageStandard")
    if standard not in standards:
        raise ConfigurationError(f"{project.path}: LanguageStandard {standard!r} has no clang-cl equivalent here")
    arguments.append(standards[standard])
    if value("ConformanceMode") == "true":
        arguments.append("/permissive-")
    if value("EnableEnhancedInstructionSet") == "AdvancedVectorExtensions2":
        arguments.append("/arch:AVX2")
    if value("FloatingPointModel") == "Precise":
        arguments.append("/fp:precise")
    if value("CharacterSet") == "Unicode":
        arguments += ["/DUNICODE", "/D_UNICODE"]
    arguments.append("/MDd" if value("UseDebugLibraries") == "true" else "/MD")
    if project.configuration_type() == "DynamicLibrary":
        arguments.append("/D_WINDLL")

    for definition in (value("PreprocessorDefinitions") or "").split(";"):
        definition = definition.strip()
        if definition and not definition.startswith("%("):
            arguments.append(f"/D{definition}")
    for option in (value("AdditionalOptions") or "").split():
        if not option.startswith("%("):
            arguments.append(option)

    macros = {
        "SolutionDir": str(root) + os.sep,
        "ProjectDir": str(root / project.directory) + os.sep,
        "VCInstallDir": os.environ.get("VCINSTALLDIR", ""),
    }
    own = os.path.normcase(os.path.normpath(root / project.directory))
    for directory in (value("AdditionalIncludeDirectories") or "").split(";"):
        directory = directory.strip()
        if not directory or directory.startswith("%("):
            continue
        expanded, unknown = BuildTree.expand_msbuild_macros(directory, macros)
        if unknown:
            raise ConfigurationError(f"{project.path}: cannot expand $({unknown[0]}) in include directory '{directory}'")
        if "$(VCInstallDir)" in directory and not macros["VCInstallDir"]:
            raise ConfigurationError(f"{project.path}: needs VCINSTALLDIR for '{directory}'; run from a Developer PowerShell")
        normalized = os.path.normpath(expanded)
        if os.path.normcase(normalized) == own:
            continue  # AGENTS.md §3: a project never lists itself; quoted includes resolve beside the file
        arguments.append(f"/I{normalized}")
    arguments += [f"/I{directory}" for directory in package_include_directories(root, project, require_restore)]
    return arguments


def collect_units(root: Path, selected: set[PurePosixPath] | None, require_build: bool = True) -> list[Unit]:
    solutions = BuildTree.find_solutions(root)
    if len(solutions) != 1:
        raise ConfigurationError(f"expected exactly one solution at the root, found {len(solutions)}")
    solution = BuildTree.load_solution(root, solutions[0])
    units: list[Unit] = []
    for path in solution.projects:
        project = BuildTree.load_project(root, path)
        sources = [item.include for item in project.items if item.kind == "ClCompile"]
        if not sources:
            continue
        if require_build and any(item.kind == "FxCompile" for item in project.items) and not (root / project.directory / "CompiledShaders").is_dir():
            raise ConfigurationError(f"{project.directory}/CompiledShaders/ does not exist; build the solution first")
        arguments = project_arguments(root, project, require_restore=require_build)
        for source in sources:
            relative = project.directory / source
            if selected is None or relative in selected:
                units.append(Unit(source=relative, project=project, arguments=arguments))
    return units


def tool_version(binary: str) -> str | None:
    try:
        output = subprocess.run([binary, "--version"], check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    match = re.search(r"version\s+(\d+\.\d+\.\d+)", output)
    return match.group(1) if match else None


def lint(root: Path, binary: str, unit: Unit) -> tuple[Unit, int, str]:
    command = [binary, "--quiet", str(root / unit.source), "--", *unit.arguments]
    result = subprocess.run(command, cwd=root, capture_output=True, text=True)
    output = (result.stdout + result.stderr).strip()
    # clang-tidy prints "N warnings generated." for warnings it suppressed by filter; that is noise.
    output = "\n".join(line for line in output.splitlines() if not re.fullmatch(r"\d+ warnings? (and \d+ errors? )?generated\.", line.strip()))
    return unit, result.returncode, output


def main() -> int:
    # A Windows console codes in cp1252, which has no box-drawing character: a report of findings must
    # not crash on its own heading and hide the findings it was about to print.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("files", nargs="*", help="limit to these .cpp files, relative to the root")
    parser.add_argument("--root", type=Path, default=BuildTree.repository_root())
    parser.add_argument("--clang-tidy", dest="binary", default="clang-tidy")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--dry-run", action="store_true", help="print each command and run nothing")
    parser.add_argument("--any-version", action="store_true", help="do not require the version CI pins")
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    selected = {PurePosixPath(f.replace("\\", "/")) for f in arguments.files} or None

    if not arguments.dry_run:
        if not os.environ.get("INCLUDE"):
            print("RunClangTidy: INCLUDE is not set. Run from a Developer PowerShell, where MSVC's environment is imported.", file=sys.stderr)
            return 2
        version = tool_version(arguments.binary)
        if version is None:
            print(f"RunClangTidy: cannot run {arguments.binary!r}. Install the pinned one: python -m pip install clang-tidy==<version>", file=sys.stderr)
            return 2
        pinned = BuildTree.pinned_tool_version(root, "CLANG_TIDY_VERSION")
        if pinned and version != pinned and not arguments.any_version:
            print(f"RunClangTidy: clang-tidy {version} is not the pinned {pinned}; findings differ between releases. "
                  f"Run: python -m pip install clang-tidy=={pinned}  (or pass --any-version)", file=sys.stderr)
            return 2

    try:
        units = collect_units(root, selected, require_build=not arguments.dry_run)
    except (ConfigurationError, OSError, BuildTree.ET.ParseError) as error:
        print(f"RunClangTidy: {error}", file=sys.stderr)
        return 2
    if selected:
        missing = selected - {unit.source for unit in units}
        if missing:
            print(f"RunClangTidy: not a translation unit of any project: {', '.join(sorted(map(str, missing)))}", file=sys.stderr)
            return 2

    if arguments.dry_run:
        for unit in units:
            print(subprocess.list2cmdline([arguments.binary, "--quiet", str(unit.source), "--", *unit.arguments]))
        return 0

    failures = 0
    with ThreadPoolExecutor(max_workers=max(1, arguments.jobs)) as pool:
        for unit, code, output in sorted(pool.map(lambda u: lint(root, arguments.binary, u), units), key=lambda r: str(r[0].source)):
            if code != 0 or output:
                print(f"── {unit.source} (exit {code})")
                if output:
                    print(output)
            if code != 0:
                failures += 1
    if failures:
        print(f"RunClangTidy: {failures} of {len(units)} translation unit(s) failed.")
        return 1
    print(f"RunClangTidy: {len(units)} translation unit(s), clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
