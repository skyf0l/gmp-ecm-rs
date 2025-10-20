#!/bin/bash

# Need to build first to generate ecm.h
# Run: cargo build
# Then run this script

set -e

# Find the most recent build directory using ls -t (works on both Linux and macOS)
BUILD_DIR=$(ls -td ../target/debug/build/gmp-ecm-sys-* 2>/dev/null | head -1)
INCLUDE_DIR="$BUILD_DIR/out/include"

if [ ! -d "$INCLUDE_DIR" ]; then
    echo "Error: Include directory not found at: $INCLUDE_DIR"
    echo "Please run 'cargo build' first to generate the headers."
    exit 1
fi

echo "Using include directory: $INCLUDE_DIR"

bindgen wrapper.h -o src/bindings.rs \
    --allowlist-function '^ecm_.*' \
    --allowlist-var '^ECM_.*' \
    --no-layout-tests \
    -- -I"$INCLUDE_DIR" -DUSE_ZLIB

echo "Bindings generated successfully!"
echo "Note: Parameter names are maintained manually - do not regenerate without preserving them."
