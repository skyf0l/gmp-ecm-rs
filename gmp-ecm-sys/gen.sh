#!/bin/bash

# Need to build first to generate ecm.h
# Run: cargo build
# Then run this script

# Find the most recent build directory
INCLUDE_DIR=$(find ../target/debug/build -name "gmp-ecm-sys-*" -type d -exec stat -c '%Y %n' {} + 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2)/out/include

# Fallback for macOS (stat has different syntax)
if [ ! -d "$INCLUDE_DIR" ]; then
    INCLUDE_DIR=$(find ../target/debug/build -name "gmp-ecm-sys-*" -type d -exec stat -f '%m %N' {} + 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2)/out/include
fi

if [ ! -d "$INCLUDE_DIR" ]; then
    echo "Error: Include directory not found. Please run 'cargo build' first."
    exit 1
fi

bindgen wrapper.h -o src/bindings.rs \
    --allowlist-function '^ecm_.*' \
    --allowlist-var '^ECM_.*' \
    --no-layout-tests \
    -- -I"$INCLUDE_DIR" -DUSE_ZLIB
