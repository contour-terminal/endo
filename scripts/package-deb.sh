#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Build and install-test the Endo Debian package in Docker, then extract the
# resulting .deb / .ddeb to a host directory.
#
# This is the single entry point used by both CI and local development, so the
# package is produced and verified the same way everywhere.
#
# Usage:
#   scripts/package-deb.sh [OUT_DIR]
#
# Environment:
#   UBUNTU_VERSION   Ubuntu release to build against (default: 26.04)
#
# Requires Docker with buildx (Docker Desktop, or `docker buildx` on Linux). On
# Windows run this from WSL or Git Bash with Docker Desktop in Linux-container mode.

set -euo pipefail

UBUNTU_VERSION="${UBUNTU_VERSION:-26.04}"
OUT_DIR="${1:-dist}"

# Resolve repository root from this script's location so it works from any CWD.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DOCKERFILE="${REPO_ROOT}/packaging/deb/Dockerfile"

# Derive the version on the host, where git works (the container build context
# excludes .git — it is often a worktree pointer that does not resolve inside the
# container). cmake/Version.cmake parses this string from version.txt.
ENDO_VERSION="$(git -C "${REPO_ROOT}" describe --tags --long --match 'v*' --dirty 2>/dev/null || echo 0.0.0)"
echo ">>> Endo version: ${ENDO_VERSION}"

echo ">>> Building & install-testing the .deb against Ubuntu ${UBUNTU_VERSION}"
docker buildx build \
    --target tester \
    --build-arg "UBUNTU_VERSION=${UBUNTU_VERSION}" \
    --build-arg "ENDO_VERSION=${ENDO_VERSION}" \
    -f "${DOCKERFILE}" \
    "${REPO_ROOT}"

echo ">>> Extracting packages to ${OUT_DIR}/"
docker buildx build \
    --target export \
    --build-arg "UBUNTU_VERSION=${UBUNTU_VERSION}" \
    --build-arg "ENDO_VERSION=${ENDO_VERSION}" \
    --output "type=local,dest=${OUT_DIR}" \
    -f "${DOCKERFILE}" \
    "${REPO_ROOT}"

echo ">>> Done. Packages in ${OUT_DIR}/:"
ls -1 "${OUT_DIR}"
