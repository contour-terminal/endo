#!/usr/bin/env python3
"""Extract and validate ```endo code blocks from documentation markdown files.

Usage:
    python3 scripts/check-doc-snippets.py [OPTIONS] [FILES...]

Options:
    --endo-path PATH   Path to endo binary (auto-detected if omitted)
    -v, --verbose      Show all blocks (not just failures)
    FILES              Specific .md files (default: all docs/**/*.md)

Markers:
    <!-- endo-no-check -->       Skip the following code block entirely
    # file: Name.endo            First line of a block — defines a virtual module file
                                 (available to subsequent blocks via --module-path)

Output verification:
    Lines containing '# => expected' are used to verify execution output.
    Blocks with # => comments are executed and their stdout is compared
    against the expected values. Blocks without # => comments are
    syntax-checked only (endo --check).
"""

import argparse
import shutil
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


def has_expected_output(source: str) -> bool:
    """Check whether source contains any # => output comments."""
    return any("# =>" in line for line in source.splitlines())


def extract_expected_output(source: str) -> list[str]:
    """Parse # => comments from source lines to build expected output.

    Each line containing '# => value' contributes 'value' to expected output.
    """
    expected = []
    for line in source.splitlines():
        idx = line.find("# =>")
        if idx != -1:
            expected.append(line[idx + 4:].strip())
    return expected


def strip_output_comments(source: str) -> str:
    """Strip # => comments from source for cleaner execution.

    The comments are after # so endo treats them as comments anyway,
    but stripping keeps the temp file clean.
    """
    cleaned = []
    for line in source.splitlines():
        idx = line.find("# =>")
        if idx != -1:
            cleaned.append(line[:idx].rstrip())
        else:
            cleaned.append(line)
    return "\n".join(cleaned)


def parse_file_annotation(source: str) -> tuple[str | None, str]:
    """Check if source starts with '# file: <filename>' annotation.

    Returns (filename, remaining_source) if annotated, or (None, source) otherwise.
    """
    lines = source.splitlines()
    if lines and lines[0].strip().startswith("# file:"):
        filename = lines[0].strip()[len("# file:"):].strip()
        remaining = "\n".join(lines[1:])
        return filename, remaining
    return None, source


def extract_code_blocks(filepath: Path) -> list[dict]:
    """Extract all ```endo code blocks from a markdown file.

    Returns a list of dicts with keys: source, line, skip, filename.
    Blocks preceded by <!-- endo-no-check --> on the previous non-empty line are marked skip.
    Blocks whose first line is '# file: Name.endo' are module definitions (filename is set).
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

            source = "\n".join(source_lines)
            filename, remaining = parse_file_annotation(source)

            blocks.append({
                "source": remaining if filename else source,
                "line": start_line,
                "skip": skip,
                "filename": filename,
            })
        i += 1
    return blocks


def build_module_cmd(endo_path: Path, module_dir: Path | None) -> list[str]:
    """Build the base endo command with optional --module-path."""
    cmd = [str(endo_path)]
    if module_dir is not None:
        cmd.extend(["--module-path", str(module_dir)])
    return cmd


def write_virtual_modules(virtual_modules: dict[str, str], module_dir: Path) -> None:
    """Write all virtual module files into the module directory."""
    for filename, content in virtual_modules.items():
        (module_dir / filename).write_text(content, encoding="utf-8")


def check_block(source: str, endo_path: Path, tmp_dir: Path,
                module_dir: Path | None = None) -> tuple[bool, str]:
    """Write source to a temp file and run endo --check on it.

    Returns (success, stderr_output).
    """
    tmp_file = tmp_dir / "snippet.endo"
    tmp_file.write_text(source, encoding="utf-8")
    cmd = build_module_cmd(endo_path, module_dir)
    cmd.extend(["--check", str(tmp_file)])
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode == 0, result.stderr.strip()
    except subprocess.TimeoutExpired:
        return False, "timeout (10s)"


def run_block(source: str, endo_path: Path, tmp_dir: Path,
              module_dir: Path | None = None) -> tuple[bool, str]:
    """Execute an endo block and verify its output against # => comments.

    Returns (success, error_message).
    """
    expected = extract_expected_output(source)
    clean_source = strip_output_comments(source)

    tmp_file = tmp_dir / "snippet.endo"
    tmp_file.write_text(clean_source, encoding="utf-8")
    cmd = build_module_cmd(endo_path, module_dir)
    cmd.append(str(tmp_file))
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except subprocess.TimeoutExpired:
        return False, "timeout (10s)"

    if result.returncode != 0:
        return False, f"exit code {result.returncode}\n{result.stderr.strip()}"

    actual_lines = result.stdout.rstrip("\n").splitlines() if result.stdout.strip() else []

    if actual_lines != expected:
        actual_str = "\n".join(f"       {line}" for line in actual_lines) or "       (empty)"
        expected_str = "\n".join(f"       {line}" for line in expected)
        return False, (
            f"output mismatch:\n"
            f"     expected:\n{expected_str}\n"
            f"     actual:\n{actual_str}"
        )

    return True, ""


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

        # Track virtual modules accumulated across blocks in this file
        virtual_modules: dict[str, str] = {}
        module_dir: Path | None = None

        for block in blocks:
            total += 1
            rel_path = md_file.relative_to(root) if md_file.is_relative_to(root) else md_file

            # Handle module-definition blocks (# file: annotation)
            if block["filename"]:
                virtual_modules[block["filename"]] = block["source"]
                skipped += 1
                if args.verbose:
                    print(f"  FILE {rel_path}:{block['line']} -> {block['filename']}")
                continue

            if block["skip"]:
                skipped += 1
                if args.verbose:
                    print(f"  SKIP {rel_path}:{block['line']}")
                continue

            # Set up module directory if we have virtual modules
            active_module_dir = None
            if virtual_modules:
                if module_dir is None:
                    module_dir = Path(tempfile.mkdtemp(prefix="endo-doc-modules-"))
                write_virtual_modules(virtual_modules, module_dir)
                active_module_dir = module_dir

            # Auto-detect: blocks with # => comments are executed and output-verified
            if has_expected_output(block["source"]):
                success, error = run_block(block["source"], endo, tmp_dir, active_module_dir)
                if success:
                    passed += 1
                    if args.verbose:
                        print(f"  PASS {rel_path}:{block['line']} (run)")
                else:
                    failed += 1
                    msg = f"  FAIL {rel_path}:{block['line']} (run)"
                    if error:
                        indent_error = "\n".join(
                            f"       {line}" for line in error.splitlines()
                        )
                        msg += f"\n{indent_error}"
                    failures.append(msg)
                    print(msg)
                continue

            # Default: syntax check only
            success, stderr = check_block(block["source"], endo, tmp_dir, active_module_dir)
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

        # Cleanup per-file module directory
        if module_dir is not None:
            shutil.rmtree(module_dir, ignore_errors=True)
            module_dir = None

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
