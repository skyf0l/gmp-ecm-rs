#!/bin/bash

# Need to build first to generate ecm.h
# Run: cargo build
# Then run this script

OUT_DIR=${OUT_DIR:-../target/debug/build/gmp-ecm-sys-*/out}
INCLUDE_DIR=$(find ../target/debug/build -name "gmp-ecm-sys-*" -type d | head -1)/out/include

if [ ! -d "$INCLUDE_DIR" ]; then
    echo "Error: Include directory not found. Please run 'cargo build' first."
    exit 1
fi

bindgen wrapper.h -o src/bindings.rs \
    --allowlist-function '^ecm_.*' \
    --allowlist-var '^ECM_.*' \
    --no-layout-tests \
    -- -I"$INCLUDE_DIR" -DUSE_ZLIB
