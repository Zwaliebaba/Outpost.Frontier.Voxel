"""Checks C++ layout against /.clang-format over the whole tree (AGENTS.md §4).

Every hand-written .h and .cpp file git knows of is checked; build output (CompiledShaders/) is
not. The clang-format version must be the one CI pins (CLANG_FORMAT_VERSION in
.github/workflows/build.yml), because different releases break long lines in different places and
a mismatched local run would disagree with the gate.

Usage:
  python Build/CheckFormat.py                      check; exit 1 if any file needs formatting
  python Build/CheckFormat.py --fix                rewrite the files that need it
  python Build/CheckFormat.py --clang-format PATH  use that binary (default: clang-format on PATH)
  python Build/CheckFormat.py --any-version        skip the version check (not what CI does)

To get the pinned binary on any machine: python -m pip install clang-format==<CLANG_FORMAT_VERSION>
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))

import BuildTree  # noqa: E402

BATCH_SIZE = 40  # files per invocation, to stay far below Windows' 32,767-character command line


def cpp_files(root: Path) -> list[PurePosixPath]:
    return [
        path
        for path in BuildTree.tracked_files(root)
        if path.suffix in (".h", ".cpp") and "CompiledShaders" not in path.parts
    ]


def clang_format_version(binary: str) -> str | None:
    try:
        output = subprocess.run([binary, "--version"], check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    match = re.search(r"version\s+(\d+\.\d+\.\d+)", output)
    return match.group(1) if match else None


def files_needing_format(root: Path, binary: str, files: list[PurePosixPath]) -> list[PurePosixPath]:
    offenders: list[PurePosixPath] = []
    for start in range(0, len(files), BATCH_SIZE):
        batch = files[start : start + BATCH_SIZE]
        result = subprocess.run(
            [binary, "--dry-run", "--Werror", "--style=file", *(str(path) for path in batch)],
            cwd=root,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            continue
        reported = {line.split(":", 1)[0].replace("\\", "/") for line in result.stderr.splitlines() if ": error:" in line}
        hits = [path for path in batch if str(path) in reported]
        if not hits:  # clang-format failed for another reason (a parse error, a bad config)
            print(result.stderr, file=sys.stderr)
            raise RuntimeError("clang-format failed without naming a file; see the output above")
        offenders.extend(hits)
    return offenders


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=BuildTree.repository_root())
    parser.add_argument("--clang-format", dest="binary", default="clang-format")
    parser.add_argument("--fix", action="store_true", help="rewrite the files that need formatting")
    parser.add_argument("--any-version", action="store_true", help="do not require the version CI pins")
    arguments = parser.parse_args()
    root = arguments.root.resolve()

    version = clang_format_version(arguments.binary)
    if version is None:
        print(f"CheckFormat: cannot run {arguments.binary!r}. Install the pinned one: python -m pip install clang-format==<version>", file=sys.stderr)
        return 2
    pinned = BuildTree.pinned_tool_version(root, "CLANG_FORMAT_VERSION")
    if pinned and version != pinned and not arguments.any_version:
        print(f"CheckFormat: clang-format {version} is not the pinned {pinned}; its line breaking differs. "
              f"Run: python -m pip install clang-format=={pinned}  (or pass --any-version)", file=sys.stderr)
        return 2

    files = cpp_files(root)
    try:
        offenders = files_needing_format(root, arguments.binary, files)
    except RuntimeError as error:
        print(f"CheckFormat: {error}", file=sys.stderr)
        return 2

    if offenders and arguments.fix:
        for start in range(0, len(offenders), BATCH_SIZE):
            batch = offenders[start : start + BATCH_SIZE]
            subprocess.run([arguments.binary, "-i", "--style=file", *(str(path) for path in batch)], cwd=root, check=True)
        print(f"CheckFormat: reformatted {len(offenders)} of {len(files)} file(s) with clang-format {version}:")
        for path in offenders:
            print(f"  {path}")
        return 0
    if offenders:
        print(f"CheckFormat: {len(offenders)} of {len(files)} file(s) need formatting (clang-format {version}); run with --fix:")
        for path in offenders:
            print(f"  {path}")
        return 1
    print(f"CheckFormat: {len(files)} file(s) formatted (clang-format {version}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
