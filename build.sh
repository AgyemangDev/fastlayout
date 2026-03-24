#!/bin/bash
# ─────────────────────────────────────────────────────────────
#  FastLayout — full rebuild + npm publish
#
#  Run from your project root:
#    chmod +x rebuild_and_publish.sh
#    ./rebuild_and_publish.sh
#
#  To publish a new version:
#    ./rebuild_and_publish.sh --publish
#
#  To do a dry-run (build only, no publish):
#    ./rebuild_and_publish.sh
# ─────────────────────────────────────────────────────────────
set -e

PUBLISH=false
for arg in "$@"; do [ "$arg" = "--publish" ] && PUBLISH=true; done

# ── Colours ───────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'
YELLOW='\033[1;33m'; BOLD='\033[1m'; NC='\033[0m'
ok()   { echo -e "${GREEN}✓${NC}  $1"; }
info() { echo -e "${CYAN}→${NC}  $1"; }
warn() { echo -e "${YELLOW}⚠${NC}  $1"; }
fail() { echo -e "${RED}✗  $1${NC}"; exit 1; }
step() { echo -e "\n${BOLD}$1${NC}"; }

echo -e "${BOLD}FastLayout — rebuild + publish${NC}"
echo    "────────────────────────────────────"

# ────────────────────────────────────────────
#  Step 1 — copy updated sources into place
# ────────────────────────────────────────────
step "Step 1 · Sync updated source files"

# engine_bindings.cpp — the heap-buffer version that fixes the OOB crash
# If you received this as engine_bindings.cpp from the fix, copy it now:
if [ -f engine_bindings_fixed.cpp ]; then
  cp engine_bindings_fixed.cpp bindings/engine_bindings.cpp
  ok "bindings/engine_bindings.cpp updated from engine_bindings_fixed.cpp"
else
  info "bindings/engine_bindings.cpp already in place (no _fixed file found)"
fi

# index.html — fixed benchmark with heap-safe WASM calls
if [ -f index_fixed.html ]; then
  cp index_fixed.html index.html
  ok "index.html updated"
fi

# ────────────────────────────────────────────
#  Step 2 — source emsdk
# ────────────────────────────────────────────
step "Step 2 · Emscripten toolchain"

if ! command -v emcc &> /dev/null; then
  for candidate in \
      "$HOME/emsdk/emsdk_env.sh" \
      "/opt/emsdk/emsdk_env.sh"  \
      "./emsdk/emsdk_env.sh"; do
    if [ -f "$candidate" ]; then
      info "Sourcing emsdk from $candidate"
      # shellcheck disable=SC1090
      source "$candidate"
      break
    fi
  done
fi

command -v emcc &> /dev/null || fail "emcc not found. Run: source ~/emsdk/emsdk_env.sh"
ok "emcc $(emcc --version | head -1 | awk '{print $3}')"

# ────────────────────────────────────────────
#  Step 3 — native C++ tests
# ────────────────────────────────────────────
step "Step 3 · Native tests"

if [ -f src/test_native.cpp ]; then
  g++ -std=c++17 -O2 -I include src/test_native.cpp -o /tmp/fl_test_native 2>&1 \
    || fail "Native compile failed"
  /tmp/fl_test_native || fail "Native tests failed"
  ok "Native tests passed"
else
  warn "src/test_native.cpp not found — skipping"
fi

# ────────────────────────────────────────────
#  Step 4 — compile C++ → WASM
# ────────────────────────────────────────────
step "Step 4 · Compile C++ → WASM"

mkdir -p dist

# Key flags explained:
#
#  ALLOW_MEMORY_GROWTH=1    — heap grows on demand, never OOB on large trees
#  MAXIMUM_MEMORY=1GB       — hard cap; enough for 50k-node trees
#  STACK_SIZE=2MB           — deep recursive trees need headroom
#  fl_free_result           — lets JS optionally release the result heap buffer
#  --closure 0              — don't mangle exported names
#  -O3                      — max throughput (use DEBUG=1 for -O1 -g)
#
#  NOTE: The benchmark page does NOT use cwrap('string') for inputs.
#  It uses _malloc + HEAPU8.set() directly, so STACK_SIZE for input
#  strings is irrelevant — but we keep it large for recursive calls.

ASSERTIONS=0
OPT_FLAGS="-O3"
if [ "${DEBUG:-0}" = "1" ]; then
  ASSERTIONS=1
  OPT_FLAGS="-O1 -g"
  warn "Debug build — slower but with assertions"
fi

info "Running emcc..."
emcc bindings/engine_bindings.cpp \
  -I include \
  -o dist/fastlayout.js \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="FastLayoutEngine" \
  -s EXPORTED_FUNCTIONS='[
    "_fl_compute_layout",
    "_fl_diff",
    "_fl_reset",
    "_fl_version",
    "_fl_free_result",
    "_malloc",
    "_free"
  ]' \
  -s EXPORTED_RUNTIME_METHODS='[
    "UTF8ToString",
    "stringToUTF8",
    "lengthBytesUTF8",
    "ccall",
    "cwrap",
    "stackAlloc",
    "stackSave",
    "stackRestore"
  ]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=33554432 \
  -s MAXIMUM_MEMORY=1073741824 \
  -s STACK_SIZE=2097152 \
  -s ENVIRONMENT="web,worker" \
  -s SINGLE_FILE=0 \
  -s WASM=1 \
  -s ASSERTIONS=$ASSERTIONS \
  --closure 0 \
  $OPT_FLAGS \
  -std=c++17

ok "WASM compiled"

# ────────────────────────────────────────────
#  Step 5 — copy JS API into dist
# ────────────────────────────────────────────
step "Step 5 · Copy JS API"

if [ -f js/fastlayout.js ]; then
  cp js/fastlayout.js dist/fastlayout.api.js
  ok "dist/fastlayout.api.js"
else
  warn "js/fastlayout.js not found — skipping API copy"
fi

# Also copy benchmark page into dist so it's served alongside the WASM
# Also copy benchmark/demo page into dist
if [ -f demo/index.html ]; then
  cp demo/index.html dist/index.html
  ok "dist/index.html (from demo/)"
else
  warn "demo/index.html not found — no UI will be available"
fi

ok "dist/index.html (benchmark)"

# ────────────────────────────────────────────
#  Step 6 — verify the build
# ────────────────────────────────────────────
step "Step 6 · Verify dist/"

[ -f dist/fastlayout.js   ] || fail "dist/fastlayout.js missing"
[ -f dist/fastlayout.wasm ] || fail "dist/fastlayout.wasm missing"

JS_SIZE=$(  du -sh dist/fastlayout.js   | cut -f1)
WASM_SIZE=$(du -sh dist/fastlayout.wasm | cut -f1)

echo ""
echo "  dist/fastlayout.js      $JS_SIZE"
echo "  dist/fastlayout.wasm    $WASM_SIZE"
[ -f dist/fastlayout.api.js ] && echo "  dist/fastlayout.api.js  $(du -sh dist/fastlayout.api.js | cut -f1)"
echo "  dist/index.html         $(du -sh dist/index.html | cut -f1)"

# Sanity: confirm _malloc and _fl_compute_layout are in the JS glue
grep -q '_malloc'            dist/fastlayout.js || fail "_malloc not exported — check EXPORTED_FUNCTIONS"
grep -q '_fl_compute_layout' dist/fastlayout.js || fail "_fl_compute_layout not exported"
grep -q '_fl_free_result'    dist/fastlayout.js || fail "_fl_free_result not exported"
ok "Export symbols verified"

# ────────────────────────────────────────────
#  Step 7 — npm publish
# ────────────────────────────────────────────
step "Step 7 · npm"

# Read current version from package.json
CURRENT_VERSION=$(node -p "require('./package.json').version" 2>/dev/null || echo "unknown")
PACKAGE_NAME=$(   node -p "require('./package.json').name"    2>/dev/null || echo "unknown")
info "Package: ${PACKAGE_NAME}@${CURRENT_VERSION}"

if $PUBLISH; then
  info "Bumping patch version..."
  npm version patch --no-git-tag-version
  NEW_VERSION=$(node -p "require('./package.json').version")
  ok "Version bumped: ${CURRENT_VERSION} → ${NEW_VERSION}"

  info "Publishing to npm..."
  npm publish --access public
  ok "Published ${PACKAGE_NAME}@${NEW_VERSION}"

  # Tag in git if this is a git repo
  if git rev-parse --is-inside-work-tree &>/dev/null; then
    git add package.json
    git commit -m "chore: release v${NEW_VERSION}" --no-verify 2>/dev/null || true
    git tag "v${NEW_VERSION}"
    ok "Git tag v${NEW_VERSION} created"
    info "Run: git push && git push --tags"
  fi
else
  echo ""
  warn "Dry run — not publishing."
  info "Run with --publish to bump version and publish:"
  echo "      ./rebuild_and_publish.sh --publish"
  echo ""
  info "Or to publish manually:"
  echo "      npm version patch"
  echo "      npm publish --access public"
fi

# ────────────────────────────────────────────
#  Done
# ────────────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}Build complete.${NC}"
echo ""
echo "  To test locally:    cd dist && npx serve ."
echo "  CDN after publish:  https://unpkg.com/${PACKAGE_NAME}/dist/fastlayout.js"
echo "  CDN (wasm):         https://unpkg.com/${PACKAGE_NAME}/dist/fastlayout.wasm"