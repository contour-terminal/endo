#!/usr/bin/env bash
# Install the Emscripten SDK for building the WASM playground.
#
# This script is idempotent: it skips the clone if ~/emsdk already exists,
# but always runs install+activate to ensure the correct version is active.
#
# Usage:
#   ./scripts/install-emscripten.sh
#   source ~/emsdk/emsdk_env.sh   # activate in your current shell
#   ./scripts/build-playground.sh  # build the WASM playground

set -euo pipefail

EMSDK_VERSION="3.1.51"
EMSDK_DIR="$HOME/emsdk"

echo "=== Emscripten SDK installer (version ${EMSDK_VERSION}) ==="

# Clone emsdk if not already present
if [[ -d "$EMSDK_DIR" ]]; then
    echo "emsdk directory already exists at ${EMSDK_DIR}, skipping clone."
else
    echo "Cloning emsdk to ${EMSDK_DIR}..."
    git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
fi

# Install and activate the pinned version
cd "$EMSDK_DIR"
echo "Installing emsdk ${EMSDK_VERSION}..."
./emsdk install "$EMSDK_VERSION"
echo "Activating emsdk ${EMSDK_VERSION}..."
./emsdk activate "$EMSDK_VERSION"

# Verify the installation works in a subshell
echo "Verifying installation..."
# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh"
if command -v emcmake &> /dev/null; then
    echo "emcmake found at: $(command -v emcmake)"
else
    echo "Error: emcmake not found after activation. Installation may have failed."
    exit 1
fi

echo ""
echo "=== Emscripten ${EMSDK_VERSION} installed successfully ==="
echo ""
echo "To use it in your current shell, run:"
echo "  source ~/emsdk/emsdk_env.sh"
echo ""
echo "Then build the playground with:"
echo "  ./scripts/build-playground.sh"
