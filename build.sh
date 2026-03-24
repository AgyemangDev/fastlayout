#!/bin/bash
set -e

echo "FastLayout WASM Build"
echo "====================="

# Auto-source emsdk if needed
if ! command -v emcc &> /dev/null; then
  if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    echo "Sourcing emsdk..."
    source "$HOME/emsdk/emsdk_env.sh"
  fi
fi

# Step 1: Native tests
echo ""
echo "Step 1: Native C++ tests..."
g++ -std=c++17 -I include src/test_native.cpp -o /tmp/fl_test_native
/tmp/fl_test_native
echo "Native tests passed"

# Step 2: Check emcc
echo ""
echo "Step 2: Checking Emscripten..."
if ! command -v emcc &> /dev/null; then
    echo "emcc not found. Run: source ~/emsdk/emsdk_env.sh"
    exit 1
fi
echo "emcc found: $(emcc --version | head -1)"

# Step 3: Compile
echo ""
echo "Step 3: Compiling C++ to WASM..."
mkdir -p dist

emcc bindings/engine_bindings.cpp \
  -I include \
  -o dist/fastlayout.js \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="FastLayoutEngine" \
  -s EXPORTED_FUNCTIONS='["_fl_compute_layout","_fl_diff","_fl_reset","_fl_version","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8","stackAlloc","stackSave","stackRestore"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=33554432 \
  -s MAXIMUM_MEMORY=536870912 \
  -s STACK_SIZE=1048576 \
  -s ENVIRONMENT="web,worker" \
  -s SINGLE_FILE=0 \
  -s WASM=1 \
  -s ASSERTIONS=0 \
  -O2 \
  -std=c++17

# Step 4: Copy JS wrapper
echo ""
echo "Step 4: Copying JS API..."
cp js/fastlayout.js dist/fastlayout.api.js

# Step 5: Sizes
echo ""
echo "Step 5: Build output:"
ls -lh dist/
echo ""
echo "Build complete. dist/ is ready."
echo "  publish: npm version patch && npm publish"