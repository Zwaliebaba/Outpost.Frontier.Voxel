"""Self-test for the three checkers: each passes over the tree as it is and fails on a planted
violation of every rule it claims.

Every test copies the repository into a temporary directory, plants one violation, runs the
checker there as CI would, and asserts the exit code and the rule it reports. Nothing in the real
tree is touched.

Usage: python Build/TestCheckers.py [-v]
CheckFormat's tests need the pinned clang-format (CLANG_FORMAT_VERSION in the workflow); set
CLANG_FORMAT to its path if it is not on PATH as clang-format. They are skipped when it is absent.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

BUILD = Path(__file__).resolve().parent
sys.path.insert(0, str(BUILD))

import BuildTree  # noqa: E402

ROOT = BuildTree.repository_root()
PYTHON = sys.executable


class TreeCase(unittest.TestCase):
    """A fresh copy of the repository per test, as a git checkout of what is tracked plus untracked
    files that are not ignored, so the checkers see what they would see in a working tree."""

    pristine: Path
    _holder: tempfile.TemporaryDirectory[str]

    @classmethod
    def setUpClass(cls) -> None:
        cls._holder = tempfile.TemporaryDirectory(prefix="checkers-")
        cls.pristine = Path(cls._holder.name) / "pristine"
        for relative in BuildTree.tracked_files(ROOT):
            target = cls.pristine / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, target)
        _git(cls.pristine, "init", "-q")
        _git(cls.pristine, "add", "-A")

    @classmethod
    def tearDownClass(cls) -> None:
        cls._holder.cleanup()

    def setUp(self) -> None:
        self._work = tempfile.TemporaryDirectory(prefix="checker-case-")
        self.tree = Path(self._work.name) / "tree"
        shutil.copytree(self.pristine, self.tree)

    def tearDown(self) -> None:
        self._work.cleanup()

    # ── helpers ──

    def run_checker(self, script: str, *arguments: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [PYTHON, str(BUILD / script), "--root", str(self.tree), *arguments],
            capture_output=True,
            text=True,
            env={**os.environ, **(env or {})},
        )

    def edit(self, relative: str, old: str, new: str, count: int = 1) -> None:
        path = self.tree / relative
        text = path.read_text(encoding="utf-8-sig")
        self.assertIn(old, text, f"{relative} no longer contains the text this test edits")
        path.write_text(text.replace(old, new, count), encoding="utf-8", newline="")

    def edit_last(self, relative: str, old: str, new: str) -> None:
        """Replaces the last occurrence, for closing tags such as </Project> that also appear nested."""
        path = self.tree / relative
        text = path.read_text(encoding="utf-8-sig")
        self.assertIn(old, text, f"{relative} no longer contains the text this test edits")
        head, _, tail = text.rpartition(old)
        path.write_text(head + new + tail, encoding="utf-8", newline="")

    def write(self, relative: str, text: str) -> None:
        path = self.tree / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="")

    def append(self, relative: str, text: str) -> None:
        path = self.tree / relative
        path.write_text(path.read_text(encoding="utf-8-sig") + text, encoding="utf-8", newline="")

    def register(self, project: str, kind: str, include: str, filter_name: str = "Universe") -> None:
        """Adds an item to a project and its .filters, as Visual Studio would."""
        self.edit(f"{project}/{project}.vcxproj", "  <ItemGroup>\n    <ProjectReference", f"  <ItemGroup>\n    <{kind} Include=\"{include}\" />\n  </ItemGroup>\n  <ItemGroup>\n    <ProjectReference")
        self.edit_last(f"{project}/{project}.vcxproj.filters", "</Project>", f"  <ItemGroup>\n    <{kind} Include=\"{include}\">\n      <Filter>{filter_name}</Filter>\n    </{kind}>\n  </ItemGroup>\n</Project>")

    def assertFinding(self, result: subprocess.CompletedProcess[str], rule: str, fragment: str = "") -> None:
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 1, f"expected a finding, got exit {result.returncode}:\n{output}")
        matching = [line for line in output.splitlines() if f"[{rule}]" in line and fragment in line]
        self.assertTrue(matching, f"no [{rule}] finding mentioning {fragment!r}:\n{output}")

    def assertClean(self, result: subprocess.CompletedProcess[str]) -> None:
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


def _git(directory: Path, *arguments: str) -> None:
    subprocess.run(["git", "-C", str(directory), *arguments], check=True, capture_output=True)


def _filters_with_world(case: TreeCase) -> None:
    """GameLogic's .filters declares Universe, which register() uses; assert it still does."""
    case.assertIn('<Filter Include="Universe">', (case.tree / "GameLogic/GameLogic.vcxproj.filters").read_text(encoding="utf-8-sig"))


class CheckProjectFilesTests(TreeCase):
    SCRIPT = "CheckProjectFiles.py"

    def check(self) -> subprocess.CompletedProcess[str]:
        return self.run_checker(self.SCRIPT)

    def test_tree_as_it_is_is_clean(self) -> None:
        self.assertClean(self.check())

    def test_source_file_missing_from_project(self) -> None:
        self.write("GameLogic/Stray.cpp", "int g_stray = 0;\r\n")
        self.assertFinding(self.check(), "registration", "Stray.cpp")

    def test_project_item_that_does_not_exist(self) -> None:
        _filters_with_world(self)
        self.register("GameLogic", "ClCompile", "Ghost.cpp")
        self.assertFinding(self.check(), "registration", "Ghost.cpp does not exist")

    def test_item_missing_from_filters(self) -> None:
        self.edit("GameLogic/GameLogic.vcxproj.filters", '<ClCompile Include="Universe.cpp">', '<ClCompile Include="Elsewhere.cpp">')
        self.assertFinding(self.check(), "filters", "Universe.cpp")

    def test_file_kind_filter(self) -> None:
        text = (self.tree / "GameLogic/GameLogic.vcxproj.filters").read_text(encoding="utf-8-sig")
        text = text.replace('<Filter Include="Universe">', '<Filter Include="Source Files">').replace("<Filter>Universe</Filter>", "<Filter>Source Files</Filter>")
        self.write("GameLogic/GameLogic.vcxproj.filters", text)
        self.assertFinding(self.check(), "filters", "Source Files")

    def test_header_in_another_filter_than_its_cpp(self) -> None:
        self.edit("NeuronCore/NeuronCore.vcxproj.filters", '<ClInclude Include="VoxFile.h">\n      <Filter>', '<ClInclude Include="VoxFile.h">\n      <Filter>X')
        self.edit("NeuronCore/NeuronCore.vcxproj.filters", '<ItemGroup>\n    <Filter Include=', '<ItemGroup>\n    <Filter Include="XFormat">\n      <UniqueIdentifier>{00000000-0000-0000-0000-000000000001}</UniqueIdentifier>\n    </Filter>\n    <Filter Include=')
        result = self.check()
        if "[filters]" in result.stdout and "different filters" in result.stdout:
            return
        self.fail(result.stdout)

    def test_header_in_a_subdirectory(self) -> None:
        self.write("GameLogic/Detail/Hidden.h", "#pragma once\r\n")
        self.assertFinding(self.check(), "flat", "Hidden.h")

    def test_file_name_not_pascal_case(self) -> None:
        self.write("GameLogic/bad_name.h", "#pragma once\r\n")
        self.register("GameLogic", "ClInclude", "bad_name.h")
        self.assertFinding(self.check(), "file-names", "bad_name.h")

    def test_disallowed_extension(self) -> None:
        self.write("GameLogic/Thing.hpp", "#pragma once\r\n")
        self.assertFinding(self.check(), "file-names", "Thing.hpp")

    def test_misnamed_shader(self) -> None:
        shutil.copy2(self.tree / "NeuronClient/Shaders/HudVS.hlsl", self.tree / "NeuronClient/Shaders/HudVertex.hlsl")
        (self.tree / "NeuronClient/Shaders/HudVS.hlsl").unlink()
        text = (self.tree / "NeuronClient/NeuronClient.vcxproj").read_text(encoding="utf-8-sig")
        self.write("NeuronClient/NeuronClient.vcxproj", text.replace("Shaders\\HudVS.hlsl", "Shaders\\HudVertex.hlsl"))
        text = (self.tree / "NeuronClient/NeuronClient.vcxproj.filters").read_text(encoding="utf-8-sig")
        self.write("NeuronClient/NeuronClient.vcxproj.filters", text.replace("Shaders\\HudVS.hlsl", "Shaders\\HudVertex.hlsl"))
        self.assertFinding(self.check(), "shaders", "HudVertex.hlsl")

    def test_compiled_shader_committed(self) -> None:
        self.write("NeuronClient/CompiledShaders/HudVS.h", "const unsigned char g_HudVS[] = {0};\r\n")
        _git(self.tree, "add", "-f", "NeuronClient/CompiledShaders/HudVS.h")
        self.assertFinding(self.check(), "flat", "CompiledShaders")

    def test_interface_prefix_on_a_type(self) -> None:
        self.edit("GameLogic/Universe.h", "} // namespace Outpost", "class ITransport\r\n{\r\n};\r\n\r\n} // namespace Outpost")
        self.assertFinding(self.check(), "type-affixes", "ITransport")

    def test_base_suffix_on_a_type(self) -> None:
        self.edit("GameLogic/Universe.h", "} // namespace Outpost", "struct TransportBase\r\n{\r\n};\r\n\r\n} // namespace Outpost")
        self.assertFinding(self.check(), "type-affixes", "TransportBase")

    def test_sdk_names_and_template_parameters_are_not_types_defined_here(self) -> None:
        self.edit(
            "GameLogic/Universe.h",
            "} // namespace Outpost",
            "struct ID3D12Device;\r\ntemplate <class CType, typename T> using Pair = CType;\r\n// class IComment { colour };\r\n\r\n} // namespace Outpost",
        )
        self.assertClean(self.check())

    def test_british_spelling_in_an_identifier(self) -> None:
        self.edit("GameLogic/Universe.h", "} // namespace Outpost", "inline constexpr int GREY_LEVEL = 1;\r\n\r\n} // namespace Outpost")
        self.assertFinding(self.check(), "spellings", "GREY_LEVEL")

    def test_release_drifts_from_debug(self) -> None:
        project = "GameLogic/GameLogic.vcxproj"
        text = (self.tree / project).read_text(encoding="utf-8-sig")
        release = text.index("'$(Configuration)|$(Platform)'=='Release|x64'\">\n    <ClCompile>")
        cut = text.index("<WarningLevel>Level4</WarningLevel>", release)
        self.write(project, text[:cut] + "<ExceptionHandling>Async</ExceptionHandling>\n      " + text[cut:])
        self.assertFinding(self.check(), "alignment", "ExceptionHandling")

    def test_required_setting_missing(self) -> None:
        self.edit("GameLogic/GameLogic.vcxproj", "<LanguageStandard>stdcpplatest</LanguageStandard>", "<LanguageStandard>stdcpp20</LanguageStandard>")
        self.assertFinding(self.check(), "settings", "LanguageStandard")

    def test_utf8_switch_missing(self) -> None:
        self.edit("GameLogic/GameLogic.vcxproj", "<AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>", "<AdditionalOptions>%(AdditionalOptions)</AdditionalOptions>")
        self.assertFinding(self.check(), "settings", "/utf-8")

    def test_win32_configuration(self) -> None:
        self.edit(
            "GameLogic/GameLogic.vcxproj",
            '<ProjectConfiguration Include="Debug|x64">',
            '<ProjectConfiguration Include="Debug|Win32">\n      <Configuration>Debug</Configuration>\n      <Platform>Win32</Platform>\n    </ProjectConfiguration>\n    <ProjectConfiguration Include="Debug|x64">',
        )
        self.assertFinding(self.check(), "configurations", "GameLogic")

    def test_solution_platform_other_than_x64(self) -> None:
        self.edit("Outpost.Frontier.slnx", '<Platform Name="x64" />', '<Platform Name="x64" />\n    <Platform Name="Win32" />')
        self.assertFinding(self.check(), "solution", "x64")

    def test_project_missing_from_solution(self) -> None:
        text = (self.tree / "Outpost.Frontier.slnx").read_text(encoding="utf-8-sig")
        self.write("Outpost.Frontier.slnx", re.sub(r'\s*<Project Path="GameLogic/GameLogic.vcxproj"[^>]*/>', "", text))
        self.assertFinding(self.check(), "solution", "GameLogic")

    def test_comment_in_project_file(self) -> None:
        self.edit("GameLogic/GameLogic.vcxproj", "<ItemGroup Label=\"ProjectConfigurations\">", "<!-- note -->\n  <ItemGroup Label=\"ProjectConfigurations\">")
        self.assertFinding(self.check(), "plain-project", "comment")

    def test_custom_target(self) -> None:
        self.edit_last("GameLogic/GameLogic.vcxproj", "</Project>", "  <Target Name=\"Extra\" />\n</Project>")
        self.assertFinding(self.check(), "plain-project", "custom target")

    def test_unapproved_package(self) -> None:
        self.edit_last("GameLogic/GameLogic.vcxproj", "</Project>", "  <Import Project=\"..\\packages\\Some.Library.1.2.3\\build\\native\\Some.Library.targets\" />\n</Project>")
        self.assertFinding(self.check(), "packages", "Some.Library")

    def test_approved_package_is_allowed(self) -> None:
        self.edit_last("GameLogic/GameLogic.vcxproj", "</Project>", "  <Import Project=\"..\\packages\\Microsoft.Native.Quic.MsQuic.Schannel.2.6.0\\build\\native\\x.targets\" />\n</Project>")
        self.write("GameLogic/packages.config", '<?xml version="1.0" encoding="utf-8"?>\r\n<packages>\r\n  <package id="Microsoft.Native.Quic.MsQuic.Schannel" version="2.6.0" targetFramework="native" />\r\n</packages>\r\n')
        self.assertClean(self.check())

    def test_unpinned_package_version(self) -> None:
        self.write("GameLogic/packages.config", '<?xml version="1.0" encoding="utf-8"?>\r\n<packages>\r\n  <package id="Microsoft.Native.Quic.MsQuic.Schannel" version="2.*" targetFramework="native" />\r\n</packages>\r\n')
        self.assertFinding(self.check(), "packages", "pinned")

    def test_project_missing_from_clang_tidy_registry(self) -> None:
        self.edit(".clang-tidy", "GameCore|GameLogic|", "GameCore|")
        self.assertFinding(self.check(), "clang-tidy", "GameLogic")

    def test_project_missing_from_agents_table(self) -> None:
        text = (self.tree / "AGENTS.md").read_text(encoding="utf-8")
        self.write("AGENTS.md", re.sub(r"^\| `Game` \| `GameLogic` \|.*\n", "", text, flags=re.MULTILINE))
        self.assertFinding(self.check(), "agents-table", "GameLogic")

    def test_library_without_test_project(self) -> None:
        text = (self.tree / "Outpost.Frontier.slnx").read_text(encoding="utf-8-sig")
        self.write("Outpost.Frontier.slnx", re.sub(r'\s*<Project Path="Tests/GameLogicTests/GameLogicTests.vcxproj"[^>]*/>', "", text))
        shutil.rmtree(self.tree / "Tests/GameLogicTests")
        text = (self.tree / "AGENTS.md").read_text(encoding="utf-8")
        self.write("AGENTS.md", re.sub(r"^\| `Tests` \| `GameLogicTests` \|.*\n", "", text, flags=re.MULTILINE))
        self.assertFinding(self.check(), "test-projects", "GameLogic")


def _pinned_clang_format() -> str | None:
    pinned = BuildTree.pinned_tool_version(ROOT, "CLANG_FORMAT_VERSION")
    for candidate in filter(None, (os.environ.get("CLANG_FORMAT"), "clang-format", f"clang-format-{(pinned or '0').split('.')[0]}")):
        try:
            output = subprocess.run([candidate, "--version"], capture_output=True, text=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            continue
        if pinned and pinned in output:
            return candidate
    return None


CLANG_FORMAT = _pinned_clang_format()


@unittest.skipIf(CLANG_FORMAT is None, "the pinned clang-format is not available")
class CheckFormatTests(TreeCase):
    SCRIPT = "CheckFormat.py"

    def check(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return self.run_checker(self.SCRIPT, "--clang-format", str(CLANG_FORMAT), *arguments)

    def test_tree_as_it_is_is_clean(self) -> None:
        self.assertClean(self.check())

    def test_misformatted_file_fails_and_fix_repairs_it(self) -> None:
        self.append("GameLogic/Universe.cpp", "int  Badly( ){return 1;}\r\n")
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("GameLogic/Universe.cpp", result.stdout)
        self.assertClean(self.check("--fix"))
        self.assertClean(self.check())

    def test_build_output_is_not_checked(self) -> None:
        self.write("NeuronClient/CompiledShaders/HudVS.h", "const unsigned char   g_HudVS[]={0};\n")
        self.assertClean(self.check())

    def test_wrong_version_is_refused(self) -> None:
        self.edit(".github/workflows/build.yml", "CLANG_FORMAT_VERSION: ", "CLANG_FORMAT_VERSION: 99.0.0 #")
        result = self.check()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("not the pinned 99.0.0", result.stderr)


class RunClangTidyTests(TreeCase):
    SCRIPT = "RunClangTidy.py"
    ENV = {"VCINSTALLDIR": "C:\\VS\\VC\\"}

    def dry_run(self, *files: str) -> subprocess.CompletedProcess[str]:
        return self.run_checker(self.SCRIPT, "--dry-run", *files, env=self.ENV)

    def test_every_translation_unit_gets_a_command(self) -> None:
        result = self.dry_run()
        self.assertClean(result)
        commands = result.stdout.splitlines()
        expected = sum(
            1
            for project in BuildTree.load_solution(self.tree, BuildTree.find_solutions(self.tree)[0]).projects
            for item in BuildTree.load_project(self.tree, project).items
            if item.kind == "ClCompile"
        )
        self.assertEqual(len(commands), expected)

    def test_command_carries_the_project_switches(self) -> None:
        result = self.dry_run("NeuronServer/TickClock.cpp")
        self.assertClean(result)
        command = result.stdout.strip()
        for switch in ("--driver-mode=cl", "/std:c++latest", "/permissive-", "/arch:AVX2", "/fp:precise", "/DUNICODE", "/D_DEBUG", "/D_LIB", "/utf-8", "/MDd"):
            self.assertIn(switch, command.split())
        self.assertIn(f"/I{os.path.normpath(self.tree / 'NeuronCore')}", command)
        self.assertNotIn(f"/I{os.path.normpath(self.tree / 'NeuronServer')}", command, "a project never lists its own directory")

    def test_test_projects_get_the_unit_test_framework(self) -> None:
        result = self.dry_run("Tests/GameLogicTests/UniverseTests.cpp")
        self.assertClean(result)
        self.assertIn("/D_WINDLL", result.stdout)
        self.assertIn("UnitTest", result.stdout)

    def fake_restore(self, project: str) -> Path:
        """Writes the build file a project's package import names, as a NuGet restore would."""
        text = (self.tree / project / f"{Path(project).name}.vcxproj").read_text(encoding="utf-8-sig")
        target = next(element.get("Project", "") for element in BuildTree.parse_xml(text).iter(f"{{{BuildTree.MSBUILD_NAMESPACE}}}Import") if "packages" in element.get("Project", ""))
        path = Path(os.path.normpath(self.tree / project / target.replace("\\", os.sep)))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n  <ItemDefinitionGroup>\n    <ClCompile>\n'
            "      <AdditionalIncludeDirectories>$(MSBuildThisFileDirectory)include/;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>\n"
            "    </ClCompile>\n  </ItemDefinitionGroup>\n</Project>\n",
            encoding="utf-8",
        )
        return path.parent / "include"

    def test_package_headers_are_on_the_include_path(self) -> None:
        include = self.fake_restore("Tools/QuicSpike")
        result = self.dry_run("Tools/QuicSpike/QuicPeer.cpp")
        self.assertClean(result)
        self.assertIn(f"/I{os.path.normpath(include)}", result.stdout.split())

    @unittest.skipIf(os.name == "nt", "the fake clang-tidy is a POSIX shell script")
    def test_findings_are_reported_on_a_cp1252_console(self) -> None:
        # A Windows console codes in cp1252, which has no box-drawing character; the report's heading
        # once crashed the runner there and hid the findings it was about to print.
        self.fake_restore("Tools/QuicSpike")
        (self.tree / "NeuronClient" / "CompiledShaders").mkdir(exist_ok=True)
        fake = self.tree / "fake-clang-tidy"
        fake.write_text(
            '#!/bin/sh\nif [ "$1" = "--version" ]; then echo "LLVM version 22.1.8"; exit 0; fi\n'
            'echo "TickClock.cpp:1:1: error: planted finding [bugprone-planted]"; exit 1\n',
            encoding="utf-8",
        )
        fake.chmod(0o755)
        result = self.run_checker(self.SCRIPT, "--clang-tidy", str(fake), "--any-version", "NeuronServer/TickClock.cpp",
                                  env={**self.ENV, "INCLUDE": "C:\\SDK", "PYTHONIOENCODING": "cp1252"})
        self.assertFinding(result, "bugprone-planted", "planted finding")
        self.assertIn("1 of 1 translation unit(s) failed", result.stdout)

    def test_real_run_needs_the_packages_restored(self) -> None:
        # Called directly: a real run first needs the pinned clang-tidy, which this job may not have.
        import RunClangTidy

        (self.tree / "NeuronClient" / "CompiledShaders").mkdir(exist_ok=True)  # as if built, so only the restore is missing
        with self.assertRaisesRegex(RunClangTidy.ConfigurationError, "not restored"):
            RunClangTidy.collect_units(self.tree, None, require_build=True)

    def test_unknown_file_is_refused(self) -> None:
        result = self.dry_run("GameLogic/NoSuchFile.cpp")
        self.assertEqual(result.returncode, 2)

    def test_unknown_macro_is_refused(self) -> None:
        self.edit("GameLogic/GameLogic.vcxproj", "$(SolutionDir)NeuronCore;", "$(NoSuchMacro)NeuronCore;")
        result = self.dry_run()
        self.assertEqual(result.returncode, 2)
        self.assertIn("NoSuchMacro", result.stderr)

    def test_real_run_needs_the_msvc_environment(self) -> None:
        environment = {key: value for key, value in os.environ.items() if key != "INCLUDE"}
        result = subprocess.run([PYTHON, str(BUILD / self.SCRIPT), "--root", str(self.tree)], capture_output=True, text=True, env=environment)
        self.assertEqual(result.returncode, 2)
        self.assertIn("INCLUDE", result.stderr)


if __name__ == "__main__":
    unittest.main()
