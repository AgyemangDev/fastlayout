#!/bin/bash
# ─────────────────────────────────────────────
#  FastLayout — WASM Build Script
#
#  Prerequisites:
#    Install Emscripten: https://emscripten.org/docs/getting_started
#    source ~/emsdk/emsdk_env.sh
#
#  Run:
#    chmod +x build.sh && ./build.sh
# ─────────────────────────────────────────────

set -e  # Exit on any error

echo "🔧 FastLayout WASM Build"
echo "═══════════════════════"

# ── Auto-source emsdk if emcc not already in PATH ─
if ! command -v emcc &> /dev/null; then
  EMSDK_ENV="$HOME/emsdk/emsdk_env.sh"
  if [ -f "$EMSDK_ENV" ]; then
    echo "⚙️  Sourcing emsdk automatically..."
    source "$EMSDK_ENV"
  fi
fi

# ── Step 1: Run native tests first ───────────
echo ""
echo "Step 1: Running native C++ tests..."
g++ -std=c++17 -I include src/test_native.cpp -o /tmp/fl_test_native
/tmp/fl_test_native
echo "✅ Native tests passed"

# ── Step 2: Check Emscripten is available ────
echo ""
echo "Step 2: Checking Emscripten..."
if ! command -v emcc &> /dev/null; then
    echo "❌ emcc not found."
    echo "   Install Emscripten: https://emscripten.org/docs/getting_started"
    echo "   Then run: source ~/emsdk/emsdk_env.sh"
    exit 1
fi
echo "✅ emcc found: $(emcc --version | head -1)"

# ── Step 3: Compile to WASM ──────────────────
echo ""
echo "Step 3: Compiling C++ → WASM..."

mkdir -p dist

emcc bindings/engine_bindings.cpp \
  -I include \
  -o dist/fastlayout.js \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="FastLayoutEngine" \
  -s EXPORTED_FUNCTIONS='["_fl_compute_layout","_fl_diff","_fl_reset","_fl_version","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s ENVIRONMENT="web,worker" \
  -s SINGLE_FILE=0 \
  -s WASM=1 \
  -O3 \
  --closure 1 \
  -std=c++17

# ── Step 4: Copy JS wrapper into dist ────────
echo ""
echo "Step 4: Copying JS API layer..."
cp js/fastlayout.js dist/fastlayout.api.js

# ── Step 5: Print output sizes ───────────────
echo ""
echo "Step 5: Build output:"
echo "─────────────────────"
ls -lh dist/
echo ""
echo "✅ Build complete → dist/"
