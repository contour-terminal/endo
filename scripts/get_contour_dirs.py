#!/usr/bin/env python3
"""Fetches the vendored source directories from the contour-terminal repository.

Which directories are fetched, from which repository, and at which ref are all
declared in scripts/contour-pin.json rather than here, so that adding a vendored
directory or moving to a new upstream branch is a data change rather than a code
change. src/CMakeLists.txt reads the same list back via --print-directories, so
the pin file stays the single source of truth.
"""

import argparse
import json
import os
import shutil
import stat
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

PIN_FILE = Path("scripts") / "contour-pin.json"

#: Environment overrides, for local experiments without editing the tracked pin file.
REF_ENV_VAR = "ENDO_CONTOUR_REF"
URL_ENV_VAR = "ENDO_CONTOUR_URL"


@dataclass(frozen=True)
class ContourPin:
    """The resolved vendoring configuration."""

    repository: str
    """Clone URL of the upstream repository."""

    ref: str
    """Branch or tag to fetch."""

    directories: tuple[str, ...]
    """Directory names under src/ to vendor, in declaration order."""


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


def load_pin(repo_root: Path, ref_override: str | None = None) -> ContourPin:
    """Reads scripts/contour-pin.json and applies the ref/URL overrides.

    Precedence for the ref is --ref, then $ENDO_CONTOUR_REF, then the pin file.

    :param repo_root: Root of the endo repository.
    :param ref_override: Ref given on the command line, if any.
    :return: The resolved configuration.
    """
    pin_path = repo_root / PIN_FILE
    data = json.loads(pin_path.read_text(encoding="utf-8"))
    return ContourPin(
        repository=os.environ.get(URL_ENV_VAR) or data["repository"],
        ref=ref_override or os.environ.get(REF_ENV_VAR) or data["ref"],
        directories=tuple(data["directories"]),
    )


def fetch(repo_root: Path, pin: ContourPin) -> None:
    """Sparse-checks-out the pinned directories and moves them into src/.

    :param repo_root: Root of the endo repository.
    :param pin: The resolved configuration.
    """
    src_dir = repo_root / "src"
    contour_dir = repo_root / "contour"

    # Clean up any previous copies
    for name in pin.directories:
        target = src_dir / name
        if target.exists():
            shutil.rmtree(target, onexc=_force_remove_readonly)

    if contour_dir.exists():
        shutil.rmtree(contour_dir, onexc=_force_remove_readonly)

    # Sparse-clone only the directories we need.
    # --depth=1 with --branch accepts a branch or a tag, but not a bare commit
    # SHA; pinning to a SHA would mean dropping --depth and checking it out
    # explicitly after the clone.
    subprocess.run(
        [
            "git", "clone",
            "-n", "--depth=1", "--filter=tree:0",
            "--branch", pin.ref,
            pin.repository,
            str(contour_dir),
        ],
        check=True,
    )
    subprocess.run(
        ["git", "sparse-checkout", "set", "--no-cone",
         *(f"src/{name}" for name in pin.directories)],
        cwd=contour_dir,
        check=True,
    )
    subprocess.run(
        ["git", "checkout"],
        cwd=contour_dir,
        check=True,
    )

    # Move the directories into place
    for name in pin.directories:
        shutil.move(str(contour_dir / "src" / name), str(src_dir / name))

    # Clean up the clone
    shutil.rmtree(contour_dir, onexc=_force_remove_readonly)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--ref",
        help=f"Branch or tag to fetch; overrides ${REF_ENV_VAR} and the pin file.",
    )
    parser.add_argument(
        "--print-directories",
        action="store_true",
        help="Print the vendored directory names as a ';'-separated list and exit "
             "(consumed by src/CMakeLists.txt).",
    )
    args = parser.parse_args()

    repo_root = get_repo_root()
    pin = load_pin(repo_root, args.ref)

    if args.print_directories:
        print(";".join(pin.directories))
        return

    print(f"Fetching {', '.join(pin.directories)} from {pin.repository} @ {pin.ref} ...")
    fetch(repo_root, pin)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {e}", file=sys.stderr)
        sys.exit(1)
    except (OSError, json.JSONDecodeError, KeyError) as e:
        print(f"Failed to read {PIN_FILE}: {e}", file=sys.stderr)
        sys.exit(1)
