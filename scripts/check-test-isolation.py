#!/usr/bin/env python3
"""Fail if a test builds fixtures at a fixed filesystem path.

`std::filesystem::temp_directory_path() / "endo_cp_test"` resolves to the same directory for
every process on the machine, so two runs of one test binary -- `ctest -j`, a developer
alongside CI, a sanitizer build alongside a normal one -- share it. Several tests also wipe
the directory before creating it, which makes a collision destructive rather than flaky.

Use an injected `InMemoryFileSystem` where the code under test accepts one, and
`endo::testing::ScopedTempDir` where a real filesystem is unavoidable.

Scoped to the directories already converted; widen SEARCH_ROOTS as others follow.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SEARCH_ROOTS = ["src/shell"]

# temp_directory_path() joined with a literal, or a hardcoded /tmp path.
PATTERNS = [
    (re.compile(r"temp_directory_path\s*\(\s*\)"), "temp_directory_path()"),
    (re.compile(r"/tmp/"), "a hardcoded /tmp path"),
]

# Paths used purely as string *values* passed to pure functions never touch disk.
#
# Every alternative here is deliberately anchored to the call or field it exempts. A bare
# identifier such as `argv` or `push_back` would exempt any line that merely happens to
# mention it -- including `dirs.push_back(fs::temp_directory_path() / "endo_x")`, exactly
# the defect this lint exists to catch.
ALLOW = re.compile(
    # Pure functions taking a path as a value.
    r"canonicalizeForHistory\(|expandForLookup\(|addValidPath\(|renderSegment\("
    r"|makeGitContext\("
    # Fixture/context struct fields and environment lookups asserted on.
    r"|\.cwd\s*=|get\(\"PWD\"\)|get\(\"OLDPWD\"\)|\.path\s*=\s*\"|\.target\s*=\s*\""
    r"|\.cellArea\b"
    # Literal argument vectors handed to argument parsers.
    r"|std::vector<std::string>\s*\{|argv\.push_back\(\"|CHECK\(paths\["
    # Rendered URIs and OSC 8 escape sequences.
    r"|file://|\\033\]8"
)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    findings: list[str] = []

    for search_root in SEARCH_ROOTS:
        for path in sorted((root / search_root).rglob("*_test.cpp")):
            for number, line in enumerate(path.read_text().splitlines(), start=1):
                if ALLOW.search(line):
                    continue
                for pattern, what in PATTERNS:
                    if pattern.search(line):
                        rel = path.relative_to(root)
                        findings.append(f"{rel}:{number}: {what}\n    {line.strip()}")
                        break

    if not findings:
        print(f"test isolation: no fixed fixture paths under {', '.join(SEARCH_ROOTS)}")
        return 0

    print("Tests must not build fixtures at a fixed filesystem path.\n")
    print("Use an injected InMemoryFileSystem, or endo::testing::ScopedTempDir when a real")
    print("filesystem is unavoidable (src/testing/ScopedTempDir.hpp).\n")
    for finding in findings:
        print(finding)
    print(f"\n{len(findings)} finding(s).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
