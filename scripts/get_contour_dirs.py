#!/usr/bin/env python3
"""Fetches crispy and vtparser source directories from the contour-terminal repository."""

import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path


def _force_remove_readonly(func, path, _exc_info):
    """Error handler for shutil.rmtree to remove read-only files (e.g. .git pack files on Windows)."""
    os.chmod(path, stat.S_IWRITE)
    func(path)


def get_repo_root() -> Path:
    """Returns the root directory of the current git repository."""
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True,
        text=True,
        check=True,
    )
    return Path(result.stdout.strip())


def main() -> None:
    repo_root = get_repo_root()
    src_dir = repo_root / "src"
    contour_dir = repo_root / "contour"

    # Clean up any previous copies
    for name in ("crispy", "vtparser"):
        target = src_dir / name
        if target.exists():
            shutil.rmtree(target, onexc=_force_remove_readonly)

    if contour_dir.exists():
        shutil.rmtree(contour_dir, onexc=_force_remove_readonly)

    # Sparse-clone only the directories we need
    subprocess.run(
        [
            "git", "clone",
            "-n", "--depth=1", "--filter=tree:0",
            "https://github.com/contour-terminal/contour.git",
            str(contour_dir),
        ],
        check=True,
    )
    subprocess.run(
        ["git", "sparse-checkout", "set", "--no-cone", "src/crispy", "src/vtparser"],
        cwd=contour_dir,
        check=True,
    )
    subprocess.run(
        ["git", "checkout"],
        cwd=contour_dir,
        check=True,
    )

    # Move the directories into place
    for name in ("crispy", "vtparser"):
        shutil.move(str(contour_dir / "src" / name), str(src_dir / name))

    # Clean up the clone
    shutil.rmtree(contour_dir, onexc=_force_remove_readonly)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {e}", file=sys.stderr)
        sys.exit(1)
