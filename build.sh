#!/bin/bash
set -e

echo "FastLayout WASM Build"
echo "====================="

# ── Auto-source emsdk if needed ───────────────
if ! command -v emcc &> /dev/null; then
  for candidate in "$HOME/emsdk/emsdk_env.sh" "/opt/emsdk/emsdk_env.sh" "./emsdk/emsdk_env.sh"; do
    if [ -f "$candidate" ]; then
      echo "Sourcing emsdk from $candidate ..."
      source "$candidate"
      break
    fi
  done
fi

if ! command -v emcc &> /dev/null; then
  echo "ERROR: emcc not found. Run: source ~/emsdk/emsdk_env.sh"
  exit 1
fi
echo "emcc: $(emcc --version | head -1)"

# ── Step 1: Native tests ──────────────────────
echo ""
echo "Step 1: Native C++ tests..."
g++ -std=c++17 -O2 -I include src/test_native.cpp -o /tmp/fl_test_native
/tmp/fl_test_native
echo "Native tests passed ✓"

# ── Step 2: Compile to WASM ───────────────────
echo ""
echo "Step 2: Compiling C++ → WASM..."
mkdir -p dist

# Key fixes vs original build:
#
#  • ALLOW_MEMORY_GROWTH=1  — prevents OOB crash when large trees
#    exhaust the initial heap.  This was the primary crash cause.
#
#  • MAXIMUM_MEMORY raised to 1 GB so 50 k-node trees never OOB.
#
#  • fl_free_result added to exports so JS can optionally reclaim
#    the heap buffer between benchmark runs.
#
#  • ASSERTIONS=1 in debug, 0 in release (toggled by $DEBUG env).
#
#  • -O3 + --closure 0 in release for max throughput without
#    mangling exported names.
#
#  • STACK_SIZE kept at 2 MB (was 1 MB) — deep recursion on 50 k
#    nodes can overflow a 1 MB stack.

ASSERTIONS=0
OPT="-O3"
if [ "${DEBUG:-0}" = "1" ]; then
  ASSERTIONS=1
  OPT="-O1 -g"
  echo "  (debug build)"
fi

emcc bindings/engine_bindings.cpp \
  -I include \
  -o dist/fastlayout.js \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="FastLayoutEngine" \
  -s EXPORTED_FUNCTIONS='["_fl_compute_layout","_fl_diff","_fl_reset","_fl_version","_fl_free_result","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8","stackAlloc","stackSave","stackRestore"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=33554432 \
  -s MAXIMUM_MEMORY=1073741824 \
  -s STACK_SIZE=2097152 \
  -s ENVIRONMENT="web,worker" \
  -s SINGLE_FILE=0 \
  -s WASM=1 \
  -s ASSERTIONS=$ASSERTIONS \
  --closure 0 \
  $OPT \
  -std=c++17

# ── Step 3: Copy JS wrapper ───────────────────
echo ""
echo "Step 3: Copying JS API..."
cp js/fastlayout.js dist/fastlayout.api.js

# ── Step 4: Sizes ─────────────────────────────
echo ""
echo "Step 4: Build output:"
ls -lh dist/
echo ""
echo "Build complete. dist/ is ready."
echo "  Publish: npm version patch && npm publish"