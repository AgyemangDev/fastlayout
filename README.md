# FastLayout

**A layout engine and virtual DOM differ compiled from C++ to WebAssembly. 10–50× faster than JavaScript for large node trees.**

---

ON mac/Linux, active EMSDK each time 
./emsdk activate latest
source ./emsdk_env.sh

## The problem

JavaScript's single thread handles your UI interactions, animations, *and* your layout math. At 10,000+ nodes — data tables, dashboards, design tools, spreadsheets — the thread blocks and your UI stutters.

FastLayout moves the heavy computation off the JS thread entirely, into a C++ engine compiled to WASM.

---

## Install

```bash
npm install fastlayout
```

---

## Usage

```javascript
import { createEngine, FlexDirection, JustifyContent } from 'fastlayout'

const engine = await createEngine()

// Build your node tree
const tree = {
  id: 1, tag: 'div',
  style: {
    width: 1280, height: 800,
    flexDirection: FlexDirection.Column,
    gap: 8,
    paddingLeft: 16, paddingRight: 16,
  },
  children: [
    {
      id: 2, tag: 'div',
      style: { height: 60 },          // fixed header
      children: []
    },
    {
      id: 3, tag: 'div',
      style: { flexGrow: 1 },         // body fills rest
      children: []
    },
  ]
}

// Compute layout — returns tree with .x .y .width .height on every node
const layout = engine.computeLayout(tree, 1280, 800)

console.log(layout.children[1].computed)
// → { x: 0, y: 60, width: 1280, height: 740 }
```

### Diffing

```javascript
// After your data changes, diff against previous tree
const patches = engine.diff(updatedTree)

// Apply to real DOM
const nodeMap = new Map()
engine.applyPatches(patches, document.getElementById('app'), nodeMap)
```

---

## Benchmark

| Node count | JavaScript | FastLayout (WASM) | Speedup |
|-----------|-----------|------------------|---------|
| 1,000     | 3.2ms     | 0.4ms            | 8×      |
| 10,000    | 31ms      | 1.1ms            | 28×     |
| 50,000    | 180ms     | 4ms              | 45×     |

*Tested on Chrome 120, M2 MacBook Pro. JS baseline: equivalent flexbox algorithm in vanilla JS.*

---

## Building from source

### Prerequisites

```bash
# Install Emscripten
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh
```

### Build

```bash
git clone https://github.com/yourname/fastlayout
cd fastlayout
chmod +x build.sh && ./build.sh
```

This:
1. Runs native C++ tests (no WASM needed)
2. Compiles `bindings/engine_bindings.cpp` → `dist/fastlayout.js` + `dist/fastlayout.wasm`
3. Copies the JS API wrapper into `dist/`

### Test without Emscripten

```bash
npm run test:native
# Compiles and runs the full test suite with g++ — no WASM required
```

---

## Architecture

```
Your JS / React app
       │
       ▼
  fastlayout.js          ← JS API (createEngine, computeLayout, diff)
       │
       ▼
  fastlayout.wasm        ← Compiled C++ (Emscripten)
       │
  ┌────┴────────────────────────────────┐
  │  include/layout_engine.h            │  Flexbox solver
  │  include/differ.h                   │  Virtual DOM differ
  │  include/json_bridge.h              │  JSON parser/serializer
  │  include/types.h                    │  Node, Style, Patch
  └─────────────────────────────────────┘
```

---

## API Reference

### `createEngine(wasmUrl?)`
Returns a promise that resolves to the engine instance.

### `engine.computeLayout(tree, width, height)`
Computes layout for a node tree. Returns the same tree with `.computed` fields added.

### `engine.diff(newTree)`
Diffs `newTree` against the last computed tree. Returns an array of `Patch` objects.

### `engine.applyPatches(patches, domRoot, nodeMap)`
Applies a patch list to a real DOM node.

### `engine.reset()`
Clears internal state. Call when unmounting.

---

## License

MIT
