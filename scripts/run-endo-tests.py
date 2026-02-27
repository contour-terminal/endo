#!/usr/bin/env python3
"""Run .endo example and test files via the endo binary, validating output.

Usage:
    python3 scripts/run-endo-tests.py [OPTIONS]

Options:
    --endo-path PATH   Path to endo binary (auto-detected if omitted)
    --dir DIR          Restrict to specific directory (default: examples/ and tests/)
    --examples-only    Run only examples/ (fast smoke test)
    -v, --verbose      Show all tests (not just failures)

Directives (parsed from file header comments):
    # description: TEXT         Test name for reporting
    # expect: VALUE             Expected stdout line (multiple allowed, ordered)
    # expect-error: MSG         Expected substring in stderr
    # expect-exit: CODE         Expected exit code (default: 0)
    # expect-nonempty           Stdout must be non-empty
    # mode: ir-only             Run with --check (compilation only)
    # mode: structured          Skip (needs shell infrastructure)
    # session-separator: MARKER Skip (needs multi-prompt REPL)
    # mock-env: NAME=VALUE      Set environment variable before execution
    # mock-which: CMD=PATH      Skip (needs mock infrastructure)
    # unused-detection: true    Run with --unused-detection flag
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path


# Files that must always be skipped (e.g. require network access)
SKIP_FILES = {
    "examples/fetch.endo",
}

# Directories whose files are always skipped
SKIP_DIRS = {
    "tests/completers",
}


def find_project_root() -> Path:
    """Walk up from script directory looking for CMakePresets.json."""
    current = Path(__file__).resolve().parent
    while current != current.parent:
        if (current / "CMakePresets.json").exists():
            return current
        current = current.parent
    sys.exit("error: could not find project root (no CMakePresets.json found)")


def find_endo_binary(root: Path) -> Path | None:
    """Search for the endo binary in common build directories."""
    candidates = [
        root / "build" / "clang-debug" / "src" / "shell" / "endo",
        root / "build" / "clang-release" / "src" / "shell" / "endo",
        root / "build" / "src" / "shell" / "endo",
    ]
    build_dir = root / "build"
    if build_dir.is_dir():
        for d in sorted(build_dir.iterdir()):
            candidate = d / "src" / "shell" / "endo"
            if candidate not in candidates:
                candidates.append(candidate)

    for candidate in candidates:
        if candidate.is_file() and candidate.stat().st_mode & 0o111:
            return candidate
    return None


def parse_directives(filepath: Path) -> dict:
    """Parse comment directives from the file header.

    Returns a dict with keys:
        description, expects, expect_errors, expect_exit, expect_nonempty,
        mode, session_separator, mock_envs, mock_whichs, unused_detection
    """
    directives = {
        "description": None,
        "expects": [],
        "expect_errors": [],
        "expect_exit": None,
        "expect_nonempty": False,
        "mode": None,
        "session_separator": None,
        "mock_envs": {},
        "mock_whichs": [],
        "unused_detection": False,
    }

    with open(filepath, encoding="utf-8") as f:
        for line in f:
            stripped = line.strip()
            if not stripped.startswith("#"):
                break

            content = stripped.lstrip("#").strip()

            if content.startswith("description:"):
                directives["description"] = content[len("description:"):].strip()
            elif content.startswith("expect-error:"):
                directives["expect_errors"].append(content[len("expect-error:"):].strip())
            elif content.startswith("expect-exit:"):
                directives["expect_exit"] = int(content[len("expect-exit:"):].strip())
            elif content.startswith("expect-nonempty"):
                directives["expect_nonempty"] = True
            elif content.startswith("expect:"):
                directives["expects"].append(content[len("expect:"):].strip())
            elif content.startswith("mode:"):
                directives["mode"] = content[len("mode:"):].strip()
            elif content.startswith("session-separator:"):
                directives["session_separator"] = content[len("session-separator:"):].strip()
            elif content.startswith("mock-env:"):
                pair = content[len("mock-env:"):].strip()
                if "=" in pair:
                    name, _, value = pair.partition("=")
                    directives["mock_envs"][name] = value
            elif content.startswith("mock-which:"):
                directives["mock_whichs"].append(content[len("mock-which:"):].strip())
            elif content.startswith("unused-detection:"):
                val = content[len("unused-detection:"):].strip().lower()
                directives["unused_detection"] = val in ("true", "1", "yes")

    return directives


def should_skip(rel_path: str, directives: dict) -> str | None:
    """Return a skip reason if the test should be skipped, or None."""
    if rel_path in SKIP_FILES:
        return "hardcoded skip (e.g. network access)"

    for skip_dir in SKIP_DIRS:
        if rel_path.startswith(skip_dir):
            return f"directory {skip_dir} skipped"

    if directives["mode"] == "structured":
        return "mode: structured (needs shell infrastructure)"

    if directives["session_separator"] is not None:
        return "session-separator (needs REPL driver)"

    if directives["mock_whichs"]:
        return "mock-which (needs mock infrastructure)"

    return None


def run_test(filepath: Path, directives: dict, endo: Path) -> tuple[bool, str]:
    """Execute a single .endo file and validate results.

    Returns (success, error_message).
    """
    env = os.environ.copy()
    for name, value in directives["mock_envs"].items():
        env[name] = value

    mode = directives["mode"]
    cmd = [str(endo)]

    if mode == "ir-only":
        cmd.append("--check")

    if directives["unused_detection"]:
        cmd.append("--unused-detection")

    cmd.append(str(filepath))

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=15,
            env=env,
        )
    except subprocess.TimeoutExpired:
        return False, "timeout (15s)"

    # Validate expect-error
    if directives["expect_errors"]:
        for expected_err in directives["expect_errors"]:
            if expected_err not in result.stderr:
                return False, (
                    f"expected stderr to contain: {expected_err!r}\n"
                    f"  actual stderr: {result.stderr.strip()!r}"
                )
        return True, ""

    # Validate exit code
    expected_exit = directives["expect_exit"] if directives["expect_exit"] is not None else 0
    if result.returncode != expected_exit:
        return False, (
            f"expected exit code {expected_exit}, got {result.returncode}\n"
            f"  stderr: {result.stderr.strip()!r}"
        )

    # For ir-only mode with no output expectations, exit code 0 is sufficient
    if mode == "ir-only" and not directives["expects"] and not directives["expect_nonempty"]:
        return True, ""

    # Validate expect-nonempty
    if directives["expect_nonempty"]:
        if not result.stdout.strip():
            return False, "expected non-empty stdout, got empty"
        return True, ""

    # Validate expect: lines
    if directives["expects"]:
        actual_lines = result.stdout.rstrip("\n").splitlines() if result.stdout.strip() else []
        # Strip trailing empty lines from both — test files conventionally end
        # with an empty `# expect:` for the REPL blank line separator, but
        # script-mode execution does not produce it.
        expected = directives["expects"][:]
        while expected and expected[-1] == "":
            expected.pop()
        while actual_lines and actual_lines[-1] == "":
            actual_lines.pop()
        if actual_lines != expected:
            actual_str = "\n".join(f"    {line}" for line in actual_lines) or "    (empty)"
            expected_str = "\n".join(f"    {line}" for line in expected)
            return False, (
                f"output mismatch:\n"
                f"  expected:\n{expected_str}\n"
                f"  actual:\n{actual_str}"
            )
        return True, ""

    # No output directives — just check exit code (already validated above)
    return True, ""


def collect_endo_files(root: Path, dirs: list[str]) -> list[Path]:
    """Recursively collect .endo files from the given directories."""
    files = []
    for d in dirs:
        dir_path = root / d
        if dir_path.is_dir():
            files.extend(sorted(dir_path.rglob("*.endo")))
    return files


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run .endo example and test files, validating output."
    )
    parser.add_argument(
        "--endo-path",
        type=Path,
        help="Path to endo binary (auto-detected if omitted)",
    )
    parser.add_argument(
        "--dir",
        type=str,
        help="Restrict to specific directory (relative to project root)",
    )
    parser.add_argument(
        "--examples-only",
        action="store_true",
        help="Run only examples/ (fast smoke test)",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Show all tests (not just failures)",
    )
    args = parser.parse_args()

    root = find_project_root()

    # Find endo binary
    if args.endo_path:
        endo = args.endo_path.resolve()
        if not endo.is_file():
            sys.exit(f"error: endo binary not found at {endo}")
    else:
        endo = find_endo_binary(root)
        if endo is None:
            sys.exit(
                "error: could not find endo binary. Build first or use --endo-path."
            )

    print(f"Using endo: {endo.relative_to(root)}")

    # Determine directories to scan
    if args.dir:
        dirs = [args.dir]
    elif args.examples_only:
        dirs = ["examples"]
    else:
        dirs = ["examples", "tests"]

    files = collect_endo_files(root, dirs)

    if not files:
        print(f"No .endo files found in: {', '.join(dirs)}")
        return 1

    passed = 0
    failed = 0
    skipped = 0
    total = 0
    failures = []

    for filepath in files:
        total += 1
        rel_path = str(filepath.relative_to(root))
        directives = parse_directives(filepath)
        desc = directives["description"] or rel_path

        # Check skip conditions
        skip_reason = should_skip(rel_path, directives)
        if skip_reason:
            skipped += 1
            if args.verbose:
                print(f"  SKIP {rel_path} ({skip_reason})")
            continue

        success, error = run_test(filepath, directives, endo)
        if success:
            passed += 1
            if args.verbose:
                print(f"  PASS {rel_path}")
        else:
            failed += 1
            msg = f"  FAIL {rel_path}"
            if desc != rel_path:
                msg += f" ({desc})"
            if error:
                indent_error = "\n".join(
                    f"       {line}" for line in error.splitlines()
                )
                msg += f"\n{indent_error}"
            failures.append(msg)
            print(msg)

    # Summary
    print(f"\nResults: {passed} passed, {failed} failed, {skipped} skipped, {total} total")

    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
