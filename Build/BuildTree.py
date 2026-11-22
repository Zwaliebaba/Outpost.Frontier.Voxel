"""The repository as the checkers see it: its files, its solution and its project files.

Shared by CheckProjectFiles.py, CheckFormat.py and RunClangTidy.py, so that all three read the
tree the same way. Standard library only: the checkers must run on a bare Python 3.10 or newer,
on Windows and on Linux, before anything else is installed.
"""

from __future__ import annotations

import re
import subprocess
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

MSBUILD_NAMESPACE = "http://schemas.microsoft.com/developer/msbuild/2003"
NS = {"m": MSBUILD_NAMESPACE}
CONFIGURATIONS = ("Debug|x64", "Release|x64")

# Directories that hold build output or IDE state. Used only when the tree is not a git checkout;
# in a checkout, .gitignore decides.
IGNORED_DIRECTORIES = {".git", ".vs", "x64", "x86", "CompiledShaders", "Captures", "TestResults", "packages", "__pycache__"}


def repository_root() -> Path:
    """The repository root: the directory above Build/, where this file lives."""
    return Path(__file__).resolve().parent.parent


def tracked_files(root: Path) -> list[PurePosixPath]:
    """Every file git knows of or would add (tracked, plus untracked files that are not ignored),
    relative to the root, with forward slashes. Falls back to a directory walk outside a checkout,
    skipping build output, so a checker also works on an exported tree."""
    try:
        listing = subprocess.run(
            ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            check=True,
            capture_output=True,
        ).stdout
        names = {name for name in listing.decode("utf-8").split("\0") if name}
        # A file deleted from the working tree but not yet from the index is still listed.
        return sorted(PurePosixPath(name) for name in names if (root / name).is_file())
    except (OSError, subprocess.CalledProcessError):
        found = []
        for path in root.rglob("*"):
            relative = path.relative_to(root)
            if path.is_file() and not any(part in IGNORED_DIRECTORIES for part in relative.parts):
                found.append(PurePosixPath(relative.as_posix()))
        return sorted(found)


def git_tracked(root: Path) -> set[PurePosixPath] | None:
    """Files in the git index only, or None outside a checkout."""
    try:
        listing = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return {PurePosixPath(name) for name in listing.decode("utf-8").split("\0") if name}


def pinned_tool_version(root: Path, variable: str) -> str | None:
    """A tool version pinned in the CI workflow, such as CLANG_FORMAT_VERSION. The workflow is the
    single place a pin is written; the checkers read it rather than repeating it."""
    workflow = root / ".github" / "workflows" / "build.yml"
    if not workflow.is_file():
        return None
    match = re.search(rf"^\s*{re.escape(variable)}:\s*['\"]?([0-9][0-9.]*)['\"]?\s*(?:#.*)?$", workflow.read_text(encoding="utf-8"), re.MULTILINE)
    return match.group(1) if match else None


def windows_path(text: str) -> PurePosixPath:
    """A path as a project file writes it (backslashes) in forward-slash form."""
    return PurePosixPath(text.replace("\\", "/"))


def strip_comments_and_strings(source: str) -> str:
    """C++ source with comments, string literals and character literals replaced by spaces, newlines
    kept, so that line numbers stay right and only code is left to scan. Raw strings are handled
    for the common R"(...)" and R"delim(...)delim" forms."""
    out: list[str] = []
    i = 0
    n = len(source)
    while i < n:
        c = source[i]
        if source.startswith("//", i):
            end = source.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif source.startswith("/*", i):
            end = source.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append(re.sub(r"[^\n]", " ", source[i:end]))
            i = end
        elif c == "R" and source.startswith('R"', i) and (i == 0 or not (source[i - 1].isalnum() or source[i - 1] == "_")):
            open_paren = source.find("(", i + 2)
            delimiter = source[i + 2 : open_paren] if open_paren >= 0 else ""
            end = source.find(")" + delimiter + '"', open_paren + 1) if open_paren >= 0 else -1
            end = n if end < 0 else end + len(delimiter) + 2
            out.append(re.sub(r"[^\n]", " ", source[i:end]))
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and source[j] != c and source[j] != "\n":
                j += 2 if source[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(" " * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


@dataclass
class ProjectItem:
    kind: str  # ClCompile, ClInclude, FxCompile, None, CopyFileToFolders, ...
    include: PurePosixPath  # as written in the project, slashes normalized
    filter: str | None = None  # from the .filters file, when read through it


@dataclass
class Project:
    """One .vcxproj and its .filters, parsed."""

    path: PurePosixPath  # relative to the repository root
    name: str
    text: str
    root: ET.Element
    items: list[ProjectItem] = field(default_factory=list)
    configurations: list[str] = field(default_factory=list)

    @property
    def directory(self) -> PurePosixPath:
        return self.path.parent

    def property_value(self, configuration: str, element: str) -> str | None:
        """The value of a property-page setting in one configuration, from the configuration's
        PropertyGroups or ItemDefinitionGroup, whichever states it."""
        for group in self.configuration_groups(configuration):
            for child in group.iter():
                if child.tag == f"{{{MSBUILD_NAMESPACE}}}{element}" and len(child) == 0:
                    return (child.text or "").strip()
        return None

    def configuration_groups(self, configuration: str) -> list[ET.Element]:
        marker = f"'$(Configuration)|$(Platform)'=='{configuration}'"
        return [group for group in self.root if group.get("Condition", "").replace(" ", "") == marker]

    def settings(self, configuration: str) -> dict[tuple[str, str], str]:
        """Every leaf setting a configuration states, keyed by (tool, property). Tool is the
        ItemDefinitionGroup child (ClCompile, Link, Lib, FxCompile) or PropertyGroup."""
        found: dict[tuple[str, str], str] = {}
        for group in self.configuration_groups(configuration):
            local = group.tag.split("}")[1]
            if local == "PropertyGroup":
                for child in group:
                    found[("PropertyGroup", child.tag.split("}")[1])] = (child.text or "").strip()
            elif local == "ItemDefinitionGroup":
                for tool in group:
                    for child in tool:
                        found[(tool.tag.split("}")[1], child.tag.split("}")[1])] = (child.text or "").strip()
        return found

    def configuration_type(self) -> str | None:
        return self.property_value("Debug|x64", "ConfigurationType")


def parse_xml(text: str) -> ET.Element:
    return ET.fromstring(text.lstrip("﻿"))


def load_project(root: Path, relative: PurePosixPath) -> Project:
    text = (root / relative).read_text(encoding="utf-8-sig")
    element = parse_xml(text)
    project = Project(path=relative, name=relative.stem, text=text, root=element)
    for configuration in element.findall("m:ItemGroup[@Label='ProjectConfigurations']/m:ProjectConfiguration", NS):
        project.configurations.append(configuration.get("Include", ""))
    for group in element.findall("m:ItemGroup", NS):
        if group.get("Label") == "ProjectConfigurations":
            continue
        for item in group:
            kind = item.tag.split("}")[1]
            if kind in ("ProjectReference", "ProjectConfiguration"):
                continue
            include = item.get("Include")
            if include:
                project.items.append(ProjectItem(kind=kind, include=windows_path(include)))
    return project


def load_filters(root: Path, project: Project) -> tuple[list[str], list[ProjectItem]] | None:
    """The filters a .filters file declares and the items it assigns, or None if there is none."""
    path = root / (str(project.path) + ".filters")
    if not path.is_file():
        return None
    element = parse_xml(path.read_text(encoding="utf-8-sig"))
    declared: list[str] = []
    items: list[ProjectItem] = []
    for group in element.findall("m:ItemGroup", NS):
        for item in group:
            kind = item.tag.split("}")[1]
            include = item.get("Include", "")
            if kind == "Filter":
                declared.append(include)
            else:
                filter_element = item.find("m:Filter", NS)
                items.append(ProjectItem(kind=kind, include=windows_path(include), filter=None if filter_element is None else (filter_element.text or "").strip()))
    return declared, items


@dataclass
class Solution:
    path: PurePosixPath
    platforms: list[str]
    projects: list[PurePosixPath]
    folders: dict[PurePosixPath, str]  # project path -> solution folder ("" at the root)


def find_solutions(root: Path) -> list[PurePosixPath]:
    return sorted(PurePosixPath(p.name) for p in root.iterdir() if p.is_file() and p.suffix in (".slnx", ".sln"))


def load_solution(root: Path, relative: PurePosixPath) -> Solution:
    element = parse_xml((root / relative).read_text(encoding="utf-8-sig"))
    platforms = [p.get("Name", "") for p in element.iter("Platform")]
    projects: list[PurePosixPath] = []
    folders: dict[PurePosixPath, str] = {}

    def walk(node: ET.Element, folder: str) -> None:
        for child in node:
            if child.tag == "Folder":
                walk(child, child.get("Name", "").strip("/"))
            elif child.tag == "Project":
                path = windows_path(child.get("Path", ""))
                projects.append(path)
                folders[path] = folder

    walk(element, "")
    return Solution(path=relative, platforms=platforms, projects=projects, folders=folders)


def expand_msbuild_macros(text: str, values: dict[str, str]) -> tuple[str, list[str]]:
    """Replaces $(Name) from values; returns the result and the names it could not expand."""
    unknown: list[str] = []

    def replace(match: re.Match[str]) -> str:
        name = match.group(1)
        if name in values:
            return values[name]
        unknown.append(name)
        return match.group(0)

    return re.sub(r"\$\(([A-Za-z_][A-Za-z0-9_]*)\)", replace, text), unknown
