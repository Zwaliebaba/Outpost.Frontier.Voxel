"""Checks the build shape and the project registries (AGENTS.md §1, §2, §3, R2, R7, R11, R14).

What it checks, each as a named rule so that a finding says which rule it broke:

  solution       one solution at the root; x64 only; every listed project exists; every project in
                 the tree is listed
  configurations every project has exactly Debug|x64 and Release|x64
  settings       the settings AGENTS.md §3, R16 and ADR-005 require, stated in both configurations
  alignment      Debug and Release differ only where AGENTS.md §3 allows
  plain-project  ADR-005: no comments, no custom targets, no imports beyond the template's own,
                 except the build files of a package R14 approves
  packages       R14: every NuGet package a project uses is on the approved table, at a pinned version
  registration   every source file under a project is in its .vcxproj, and every item exists
  filters        every item is in the .filters file under a declared filter; no file-kind filters;
                 a header sits in the same filter as its .cpp
  flat           C++ sources directly in their project folder; only Shaders/ beside them; build output
                 (CompiledShaders/) never committed
  shaders        Shaders/<Name>VS.hlsl and Shaders/<Name>PS.hlsl; shared code in Shaders/*.hlsli
  file-names     R7: PascalCase .h and .cpp, nothing else
  type-affixes   R2: no I/C/S/E prefix, no Base/Abstract/Impl/_t suffix on a type this tree defines
  spellings      R11: the SDK's spelling in identifiers (color, not colour)
  clang-tidy     every project's headers are matched by .clang-tidy's HeaderFilterRegex
  agents-table   every project is in AGENTS.md §2's table, and the table lists no project that is gone
  test-projects  ADR-006: every library has Tests/<Library>Tests/<Library>Tests.vcxproj

Usage: python Build/CheckProjectFiles.py [--root DIR]
Exit code 0 when clean, 1 when there are findings, 2 when the tree cannot be read.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))

import BuildTree  # noqa: E402  (after the path insert, so the script runs from any directory)

# ── Rules as data ──────────────────────────────────────────────────────────────────────────────

# AGENTS.md §3, R16 and ADR-005: stated in both configurations of every project. (tool, property)
REQUIRED_SETTINGS = {
    ("PropertyGroup", "PlatformToolset"): "v145",
    ("ClCompile", "WarningLevel"): "Level4",
    ("ClCompile", "TreatWarningAsError"): "true",
    ("ClCompile", "SDLCheck"): "true",
    ("ClCompile", "ConformanceMode"): "true",
    ("ClCompile", "LanguageStandard"): "stdcpplatest",
    ("ClCompile", "FloatingPointModel"): "Precise",
    ("ClCompile", "EnableEnhancedInstructionSet"): "AdvancedVectorExtensions2",
    ("ClCompile", "MultiProcessorCompilation"): "true",
}
REQUIRED_OPTIONS = {("ClCompile", "AdditionalOptions"): "/utf-8"}

# AGENTS.md §3: the whole of what may differ between Debug and Release, in MSBuild's spelling.
# PreprocessorDefinitions is compared with _DEBUG and NDEBUG taken out.
ALLOWED_DIFFERENCES = {
    "Optimization",
    "UseDebugLibraries",
    "RuntimeLibrary",
    "FunctionLevelLinking",
    "IntrinsicFunctions",
    "LinkIncremental",
    "WholeProgramOptimization",
    "EnableCOMDATFolding",
    "OptimizeReferences",
    "LinkTimeCodeGeneration",
}

# ADR-005: the imports Visual Studio's C++ template writes, and nothing else.
TEMPLATE_IMPORTS = {
    r"$(VCTargetsPath)\Microsoft.Cpp.Default.props",
    r"$(VCTargetsPath)\Microsoft.Cpp.props",
    r"$(VCTargetsPath)\Microsoft.Cpp.targets",
    r"$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props",
}
PACKAGE_IMPORT = re.compile(r"(?:^|[\\/])packages[\\/](?P<id>[A-Za-z0-9_.-]+?)\.(?P<version>\d+(?:\.\d+)+(?:-[A-Za-z0-9.]+)?)[\\/]", re.IGNORECASE)

FILE_KIND_FILTERS = {"source files", "header files", "resource files"}
WIZARD_FILE_NAMES = {"pch.h", "pch.cpp", "framework.h", "targetver.h", "Resource.h"}
PASCAL_FILE = re.compile(r"^[A-Z][A-Za-z0-9]*\.(h|cpp)$")
SHADER_FILE = re.compile(r"^Shaders/[A-Z][A-Za-z0-9]*(VS|PS)\.hlsl$")
SHADER_INCLUDE = re.compile(r"^Shaders/[A-Z][A-Za-z0-9]*\.hlsli$")
CPP_EXTENSIONS = {".h", ".cpp", ".hpp", ".cc", ".cxx", ".inl", ".hh", ".c"}
SOURCE_EXTENSIONS = CPP_EXTENSIONS | {".hlsl", ".hlsli"}

# R2. A type defined here, not one merely declared or named from an SDK.
TYPE_DEFINITION = re.compile(
    r"\b(?:class|struct|union|enum(?:\s+(?:class|struct))?|concept)\s+(?:\[\[[^\]]*\]\]\s*)?(?P<name>[A-Za-z_]\w*)\s*(?P<next>[^\s])"
)
TYPE_ALIAS = re.compile(r"\busing\s+(?P<name>[A-Za-z_]\w*)\s*=")
BANNED_PREFIX = re.compile(r"^[ICSE][A-Z][a-z0-9]")
BANNED_SUFFIX = re.compile(r"(?:[a-z0-9](?:Base|Abstract|Impl)|_t)$")
BANNED_LEADING_WORD = re.compile(r"^(?:Base|Abstract)[A-Z]")

# R11. The non-SDK half of each spelling family, as it appears inside an identifier.
NON_SDK_SPELLINGS = {
    "colour": "color",
    "initialis": "initializ",
    "serialis": "serializ",
    "normalis": "normaliz",
    "quantis": "quantiz",
    "synchronis": "synchroniz",
    "behaviour": "behavior",
    "neighbour": "neighbor",
    "centre": "center",
    "grey": "gray",
    "cancelled": "canceled",
    "cancelling": "canceling",
}
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


class Findings:
    def __init__(self) -> None:
        self.items: list[tuple[str, str, str]] = []

    def add(self, rule: str, where: object, message: str) -> None:
        self.items.append((rule, str(where), message))

    def report(self) -> int:
        for rule, where, message in sorted(self.items):
            print(f"{where}: [{rule}] {message}")
        return len(self.items)


# ── Checks ─────────────────────────────────────────────────────────────────────────────────────


def check_solution(root: Path, files: list[PurePosixPath], findings: Findings) -> BuildTree.Solution | None:
    solutions = BuildTree.find_solutions(root)
    if len(solutions) != 1:
        findings.add("solution", root.name, f"expected exactly one .slnx or .sln at the root, found {len(solutions)}")
        if not solutions:
            return None
    solution = BuildTree.load_solution(root, solutions[0])
    if solution.platforms != ["x64"]:
        findings.add("solution", solution.path, f"platforms must be exactly x64, found {solution.platforms or 'none'}")
    listed = set(solution.projects)
    for project in solution.projects:
        if not (root / project).is_file():
            findings.add("solution", solution.path, f"lists {project}, which does not exist")
    for path in files:
        if path.suffix == ".vcxproj" and path not in listed:
            findings.add("solution", path, f"is not in {solution.path}")
    return solution


def check_project_settings(project: BuildTree.Project, findings: Findings) -> None:
    if sorted(project.configurations) != sorted(BuildTree.CONFIGURATIONS):
        findings.add("configurations", project.path, f"must have exactly {', '.join(BuildTree.CONFIGURATIONS)}, has {project.configurations}")
    settings = {configuration: project.settings(configuration) for configuration in BuildTree.CONFIGURATIONS}
    for configuration, values in settings.items():
        for key, expected in REQUIRED_SETTINGS.items():
            if values.get(key) != expected:
                findings.add("settings", project.path, f"{configuration}: {key[0]}.{key[1]} must be {expected}, is {values.get(key) or 'not stated'}")
        for key, option in REQUIRED_OPTIONS.items():
            if option not in (values.get(key) or "").split():
                findings.add("settings", project.path, f"{configuration}: {key[0]}.{key[1]} must contain {option}")
    debug, release = settings["Debug|x64"], settings["Release|x64"]
    for key in sorted(set(debug) | set(release)):
        if key[1] in ALLOWED_DIFFERENCES:
            continue
        a, b = debug.get(key), release.get(key)
        if key[1] == "PreprocessorDefinitions":
            a = ";".join(d for d in (a or "").split(";") if d not in ("_DEBUG", "NDEBUG"))
            b = ";".join(d for d in (b or "").split(";") if d not in ("_DEBUG", "NDEBUG"))
            if "_DEBUG" not in (debug.get(key) or "") or "NDEBUG" not in (release.get(key) or ""):
                findings.add("alignment", project.path, "PreprocessorDefinitions must carry _DEBUG in Debug and NDEBUG in Release")
        if a != b:
            findings.add("alignment", project.path, f"{key[0]}.{key[1]} differs: Debug {a!r}, Release {b!r}; AGENTS.md §3 allows only optimisation settings to differ")


def approved_packages(root: Path) -> set[str]:
    """Package identifiers in R14's approved table in AGENTS.md: the backticked second column."""
    agents = root / "AGENTS.md"
    if not agents.is_file():
        return set()
    text = agents.read_text(encoding="utf-8")
    start = text.find("**R14")
    end = text.find("**R15", start)
    section = text[start:end] if start >= 0 else ""
    return {m.group(1).lower() for m in re.finditer(r"^\|[^|\n]*\|\s*`([A-Za-z0-9_.-]+)`\s*\|", section, re.MULTILINE)}


def check_plain_project(root: Path, project: BuildTree.Project, approved: set[str], findings: Findings) -> None:
    for name, text in ((project.path, project.text), (str(project.path) + ".filters", _read(root / (str(project.path) + ".filters")))):
        if text and "<!--" in text:
            findings.add("plain-project", name, "contains a comment; ADR-005 keeps project files exactly as Visual Studio writes them")
    if project.root.find("m:Target", BuildTree.NS) is not None:
        findings.add("plain-project", project.path, "defines a custom target (ADR-005)")
    for element in project.root.iter(f"{{{BuildTree.MSBUILD_NAMESPACE}}}Import"):
        target = element.get("Project", "")
        if target in TEMPLATE_IMPORTS:
            continue
        package = PACKAGE_IMPORT.search(target)
        if package is None:
            findings.add("plain-project", project.path, f"imports {target}, which is neither the template's nor a package's (ADR-005)")
        elif package.group("id").lower() not in approved:
            findings.add("packages", project.path, f"imports the build files of {package.group('id')}, which is not on R14's approved table")
    packages_config = root / project.directory / "packages.config"
    if packages_config.is_file():
        element = BuildTree.parse_xml(packages_config.read_text(encoding="utf-8-sig"))
        for package in element.iter("package"):
            identifier, version = package.get("id", ""), package.get("version", "")
            if identifier.lower() not in approved:
                findings.add("packages", packages_config.relative_to(root).as_posix(), f"uses {identifier}, which is not on R14's approved table")
            if not re.fullmatch(r"\d+(\.\d+)+(-[A-Za-z0-9.]+)?", version):
                findings.add("packages", packages_config.relative_to(root).as_posix(), f"{identifier} must be pinned to an exact version, is {version!r}")


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig") if path.is_file() else ""


def check_items(root: Path, project: BuildTree.Project, files: list[PurePosixPath], tracked: set[PurePosixPath] | None, findings: Findings) -> None:
    directory = project.directory
    registered: set[PurePosixPath] = set()
    for item in project.items:
        resolved = PurePosixPath(*_normalize(directory / item.include))
        registered.add(resolved)
        if not (root / resolved).is_file():
            findings.add("registration", project.path, f"{item.kind} {item.include} does not exist")
        relative = item.include.as_posix()
        if item.kind in ("ClCompile", "ClInclude"):
            if "/" in relative:
                findings.add("flat", project.path, f"{relative} is in a subdirectory; C++ sources sit directly in the project folder (AGENTS.md §2)")
            elif relative not in WIZARD_FILE_NAMES and not PASCAL_FILE.match(relative):
                findings.add("file-names", project.path, f"{relative} is not a PascalCase .h or .cpp file (R7)")
        elif item.kind == "FxCompile" and not SHADER_FILE.match(relative):
            findings.add("shaders", project.path, f"{relative} must be Shaders/<Name>VS.hlsl or Shaders/<Name>PS.hlsl (AGENTS.md §2)")
        elif item.kind == "None" and relative.endswith(".hlsli") and not SHADER_INCLUDE.match(relative):
            findings.add("shaders", project.path, f"{relative} must be Shaders/<Name>.hlsli (AGENTS.md §2)")
    for path in files:
        if directory not in path.parents or path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue
        inside = path.relative_to(directory)
        if inside.parts[0] == "CompiledShaders":
            continue
        if path.suffix.lower() in CPP_EXTENSIONS and len(inside.parts) > 1:
            findings.add("flat", path, "is a C++ file in a subdirectory; clang-tidy's HeaderFilterRegex never sees it (AGENTS.md §2)")
        elif path.suffix.lower() in (".hlsl", ".hlsli") and inside.parts[0] != "Shaders":
            findings.add("shaders", path, "shader source outside Shaders/ (AGENTS.md §2)")
        if path.suffix.lower() in (".hpp", ".cc", ".cxx", ".inl", ".hh", ".c"):
            findings.add("file-names", path, "uses an extension R7 does not allow; use .h or .cpp")
        if path not in registered:
            findings.add("registration", path, f"is not in {project.path}")
    if tracked is not None:
        for path in tracked:
            if directory in path.parents and path.relative_to(directory).parts[0] == "CompiledShaders":
                findings.add("flat", path, "is build output in CompiledShaders/ and must not be committed (AGENTS.md §2)")


def _normalize(path: PurePosixPath) -> list[str]:
    parts: list[str] = []
    for part in path.parts:
        if part == "..":
            if parts:
                parts.pop()
        elif part != ".":
            parts.append(part)
    return parts


def check_filters(root: Path, project: BuildTree.Project, findings: Findings) -> None:
    loaded = BuildTree.load_filters(root, project)
    filters_path = str(project.path) + ".filters"
    if loaded is None:
        findings.add("filters", project.path, "has no .filters file")
        return
    declared, items = loaded
    for name in declared:
        if name.strip().lower() in FILE_KIND_FILTERS:
            findings.add("filters", filters_path, f"declares the file-kind filter {name!r}; filters say what code does (AGENTS.md §2)")
    in_project = {(item.kind, item.include) for item in project.items}
    in_filters = {(item.kind, item.include): item for item in items}
    for key in sorted(in_project - set(in_filters), key=str):
        findings.add("filters", filters_path, f"is missing {key[0]} {key[1]}")
    for key in sorted(set(in_filters) - in_project, key=str):
        findings.add("filters", filters_path, f"lists {key[0]} {key[1]}, which is not in the project")
    stems: dict[str, set[str]] = defaultdict(set)
    for item in items:
        if not item.filter:
            findings.add("filters", filters_path, f"{item.kind} {item.include} has no filter")
        elif item.filter not in declared:
            findings.add("filters", filters_path, f"{item.kind} {item.include} is in undeclared filter {item.filter!r}")
        if item.kind in ("ClCompile", "ClInclude") and item.filter:
            stems[item.include.stem].add(item.filter)
    for stem, groups in sorted(stems.items()):
        if len(groups) > 1:
            findings.add("filters", filters_path, f"{stem}.h and {stem}.cpp are in different filters {sorted(groups)}; a header sits with its .cpp")


def check_sources(root: Path, files: list[PurePosixPath], project_dirs: set[PurePosixPath], findings: Findings) -> None:
    """R2 and R11, over every hand-written C++ file in a project folder."""
    for path in files:
        if path.suffix not in (".h", ".cpp") or not any(d in path.parents for d in project_dirs) or "CompiledShaders" in path.parts:
            continue
        code = BuildTree.strip_comments_and_strings((root / path).read_text(encoding="utf-8-sig", errors="replace"))
        for match in TYPE_DEFINITION.finditer(code):
            name, following = match.group("name"), match.group("next")
            if following in ";,>=)" or following == ".":  # declaration, template parameter, or elaborated use
                continue
            _check_type_name(path, code, match.start(), name, findings)
        for match in TYPE_ALIAS.finditer(code):
            _check_type_name(path, code, match.start(), match.group("name"), findings)
        for line_number, line in enumerate(code.splitlines(), start=1):
            if line.lstrip().startswith("#include"):
                continue
            for token in IDENTIFIER.findall(line):
                lowered = token.lower()
                for wrong, right in NON_SDK_SPELLINGS.items():
                    if wrong in lowered:
                        findings.add("spellings", f"{path}:{line_number}", f"identifier {token!r} spells {wrong!r}; the SDK's family is {right!r} (R11)")


def _check_type_name(path: PurePosixPath, code: str, offset: int, name: str, findings: Findings) -> None:
    line_number = code.count("\n", 0, offset) + 1
    where = f"{path}:{line_number}"
    if BANNED_PREFIX.match(name):
        findings.add("type-affixes", where, f"type {name!r} carries a {name[0]}-prefix; name the concept (R2)")
    if BANNED_SUFFIX.search(name) or BANNED_LEADING_WORD.match(name):
        findings.add("type-affixes", where, f"type {name!r} carries a Base/Abstract/Impl/_t affix; name the concept (R2)")


def check_clang_tidy_registry(root: Path, projects: list[BuildTree.Project], findings: Findings) -> None:
    config = root / ".clang-tidy"
    text = _read(config)
    match = re.search(r"^HeaderFilterRegex:\s*'(?P<regex>[^']*)'", text, re.MULTILINE)
    if match is None:
        findings.add("clang-tidy", ".clang-tidy", "has no HeaderFilterRegex")
        return
    regex = re.compile(match.group("regex"))
    for project in projects:
        sample = f"{project.directory.as_posix()}/Sample.h"
        if not regex.search(sample) or not regex.search(sample.replace("/", "\\")):
            findings.add("clang-tidy", ".clang-tidy", f"HeaderFilterRegex does not match {project.name}'s headers, so nothing lints them")


def check_agents_table(root: Path, projects: list[BuildTree.Project], findings: Findings) -> None:
    agents = root / "AGENTS.md"
    text = _read(agents)
    start = text.find("## 2. Repository shape")
    end = text.find("\n## ", start + 1)
    section = text[start:end] if start >= 0 else ""
    listed = set(re.findall(r"^\|[^|\n]*\|\s*`([A-Za-z0-9]+)`\s*\|", section, re.MULTILINE))
    names = {project.name for project in projects}
    for name in sorted(names - listed):
        findings.add("agents-table", "AGENTS.md", f"§2's project table does not list {name}")
    for name in sorted(listed - names):
        findings.add("agents-table", "AGENTS.md", f"§2's project table lists {name}, which is not in the solution")


def check_layout_and_tests(projects: list[BuildTree.Project], findings: Findings) -> None:
    by_name = {project.name: project for project in projects}
    for project in projects:
        if project.directory.name != project.name:
            findings.add("flat", project.path, f"lives in {project.directory}, not a folder named {project.name}")
        if project.name.endswith("Tests") and project.directory.parent != PurePosixPath("Tests"):
            findings.add("test-projects", project.path, "test projects live in Tests/<Library>Tests/ (ADR-006)")
        if project.configuration_type() == "StaticLibrary":
            expected = PurePosixPath("Tests") / f"{project.name}Tests" / f"{project.name}Tests.vcxproj"
            tests = by_name.get(f"{project.name}Tests")
            if tests is None or tests.path != expected:
                findings.add("test-projects", project.path, f"library has no test project at {expected} (ADR-006)")


# ── Driver ─────────────────────────────────────────────────────────────────────────────────────


def run(root: Path) -> int:
    findings = Findings()
    files = BuildTree.tracked_files(root)
    tracked = BuildTree.git_tracked(root)
    solution = check_solution(root, files, findings)
    if solution is None:
        return findings.report()
    projects = [BuildTree.load_project(root, path) for path in solution.projects if (root / path).is_file()]
    approved = approved_packages(root)
    for project in projects:
        check_project_settings(project, findings)
        check_plain_project(root, project, approved, findings)
        check_items(root, project, files, tracked, findings)
        check_filters(root, project, findings)
    check_sources(root, files, {project.directory for project in projects}, findings)
    check_clang_tidy_registry(root, projects, findings)
    check_agents_table(root, projects, findings)
    check_layout_and_tests(projects, findings)
    count = findings.report()
    if count == 0:
        print(f"CheckProjectFiles: {len(projects)} projects, {len(files)} files, clean.")
    else:
        print(f"CheckProjectFiles: {count} finding(s).")
    return count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=BuildTree.repository_root(), help="repository root (default: the parent of Build/)")
    arguments = parser.parse_args()
    try:
        return 1 if run(arguments.root.resolve()) else 0
    except (OSError, BuildTree.ET.ParseError) as error:
        print(f"CheckProjectFiles: cannot read the tree: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
