#!/usr/bin/env python3
"""Extract and validate ```endo code blocks from documentation markdown files.

Usage:
    python3 scripts/check-doc-snippets.py [OPTIONS] [FILES...]

Options:
    --endo-path PATH   Path to endo binary (auto-detected if omitted)
    -v, --verbose      Show all blocks (not just failures)
    FILES              Specific .md files (default: all docs/**/*.md)
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


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
    # Also try any build/*/src/shell/endo
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


def extract_code_blocks(filepath: Path) -> list[dict]:
    """Extract all ```endo code blocks from a markdown file.

    Returns a list of dicts with keys: source, line, skip.
    Blocks preceded by <!-- endo-no-check --> on the previous non-empty line are marked skip.
    """
    blocks = []
    lines = filepath.read_text(encoding="utf-8").splitlines()
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped.startswith("```endo"):
            # Check if previous non-empty line is the skip marker
            skip = False
            for j in range(i - 1, -1, -1):
                prev = lines[j].strip()
                if prev == "<!-- endo-no-check -->":
                    skip = True
                    break
                if prev:  # non-empty, non-marker line
                    break

            start_line = i + 1  # 1-indexed line of the opening fence
            i += 1
            source_lines = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                source_lines.append(lines[i])
                i += 1
            blocks.append({
                "source": "\n".join(source_lines),
                "line": start_line,
                "skip": skip,
            })
        i += 1
    return blocks


def check_block(source: str, endo_path: Path, tmp_dir: Path) -> tuple[bool, str]:
    """Write source to a temp file and run endo --check on it.

    Returns (success, stderr_output).
    """
    tmp_file = tmp_dir / "snippet.endo"
    tmp_file.write_text(source, encoding="utf-8")
    try:
        result = subprocess.run(
            [str(endo_path), "--check", str(tmp_file)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode == 0, result.stderr.strip()
    except subprocess.TimeoutExpired:
        return False, "timeout (10s)"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check ```endo code blocks in documentation for validity."
    )
    parser.add_argument(
        "--endo-path",
        type=Path,
        help="Path to endo binary (auto-detected if omitted)",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Show all blocks (not just failures)",
    )
    parser.add_argument(
        "--allow-failures",
        action="store_true",
        help="Exit 0 even if blocks fail (report only)",
    )
    parser.add_argument(
        "files",
        nargs="*",
        type=Path,
        help="Specific .md files (default: all docs/**/*.md)",
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

    # Collect markdown files
    if args.files:
        md_files = sorted(args.files)
    else:
        md_files = sorted((root / "docs").rglob("*.md"))

    # Use project-local tmp/ directory
    tmp_dir = root / "tmp"
    tmp_dir.mkdir(exist_ok=True)

    passed = 0
    failed = 0
    skipped = 0
    total = 0
    failures = []

    for md_file in md_files:
        blocks = extract_code_blocks(md_file)
        for block in blocks:
            total += 1
            rel_path = md_file.relative_to(root) if md_file.is_relative_to(root) else md_file

            if block["skip"]:
                skipped += 1
                if args.verbose:
                    print(f"  SKIP {rel_path}:{block['line']}")
                continue

            success, stderr = check_block(block["source"], endo, tmp_dir)
            if success:
                passed += 1
                if args.verbose:
                    print(f"  PASS {rel_path}:{block['line']}")
            else:
                failed += 1
                indent_stderr = "\n".join(
                    f"       {line}" for line in stderr.splitlines()
                ) if stderr else ""
                msg = f"  FAIL {rel_path}:{block['line']}"
                if indent_stderr:
                    msg += f"\n{indent_stderr}"
                failures.append(msg)
                print(msg)

    # Summary
    print(f"\nResults: {passed} passed, {failed} failed, {skipped} skipped, {total} total")

    # Cleanup temp file
    snippet_file = tmp_dir / "snippet.endo"
    if snippet_file.exists():
        snippet_file.unlink()

    if args.allow_failures:
        return 0
    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
