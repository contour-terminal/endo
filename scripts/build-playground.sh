#!/usr/bin/env bash
# Build the Endo WASM playground locally.
#
# Prerequisites:
#   - Emscripten SDK installed and activated (source emsdk_env.sh)
#
# Usage:
#   ./scripts/build-playground.sh
#   mkdocs serve

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Check for Emscripten
if ! command -v emcmake &> /dev/null; then
    echo "Error: Emscripten not found. Please install and activate the Emscripten SDK:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk && ./emsdk install latest && ./emsdk activate latest"
    echo "  source emsdk_env.sh"
    exit 1
fi

echo "=== Configuring Emscripten build ==="
cd "$PROJECT_DIR"
emcmake cmake --preset emscripten-release

echo "=== Building WASM playground ==="
cmake --build --preset emscripten-release --target endo-wasm

echo "=== Copying artifacts to docs/ ==="
mkdir -p "$PROJECT_DIR/docs/assets/js"
cp "$PROJECT_DIR/build/emscripten-release/src/endo-language/endo-playground.js" "$PROJECT_DIR/docs/assets/js/"
cp "$PROJECT_DIR/build/emscripten-release/src/endo-language/endo-playground.wasm" "$PROJECT_DIR/docs/assets/js/"

echo ""
echo "=== Done! ==="
echo "WASM artifacts copied to docs/assets/js/"
echo ""
echo "To preview the playground locally:"
echo "  mkdocs serve"
echo "  Open http://127.0.0.1:8000/playground/"
