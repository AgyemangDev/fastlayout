/**
 * FastLayout — JavaScript API
 *
 * This is what developers install and use.
 * They never touch WASM or C++ directly.
 *
 * Usage:
 *   import { createEngine } from 'fastlayout'
 *
 *   const engine = await createEngine()
 *   const layout = engine.computeLayout(myTree, 1280, 800)
 *   const patches = engine.diff(myNewTree)
 */

// ── Constants matching C++ enums ──────────────
export const FlexDirection = {
  Row:           0,
  Column:        1,
  RowReverse:    2,
  ColumnReverse: 3,
}

export const JustifyContent = {
  Start:        0,
  End:          1,
  Center:       2,
  SpaceBetween: 3,
  SpaceAround:  4,
  SpaceEvenly:  5,
}

export const AlignItems = {
  Start:    0,
  End:      1,
  Center:   2,
  Stretch:  3,
  Baseline: 4,
}

export const PatchOp = {
  Insert:      0,
  Remove:      1,
  Replace:     2,
  UpdateAttrs: 3,
  UpdateText:  4,
  Move:        5,
}

// ── Node builder helpers ───────────────────────
// These let devs build trees without JSON manually

let _nextId = 1
export function createNode(tag, style = {}, children = [], attrs = {}) {
  return {
    id: _nextId++,
    tag,
    style,
    children,
    attrs,
    text: '',
  }
}

export function createText(content, style = {}) {
  return { ...createNode('text', style), text: content }
}

// ── Engine factory ────────────────────────────
export async function createEngine(wasmUrl = './fastlayout.wasm') {
  // In browser: load WASM from URL
  // In Node/test: load from filesystem
  let wasmModule

  if (typeof FastLayoutEngine !== 'undefined') {
    // Already loaded via <script> tag
    wasmModule = await FastLayoutEngine()
  } else if (typeof require !== 'undefined') {
    // Node.js — for testing without browser
    wasmModule = _createMockEngine()
  } else {
    throw new Error(
      'FastLayoutEngine not found. ' +
      'Include fastlayout.js before this module, or use a bundler.'
    )
  }

  // ── Bind the C functions ─────────────────────
  const _computeLayout = wasmModule.cwrap('fl_compute_layout', 'string', ['string'])
  const _diff          = wasmModule.cwrap('fl_diff',           'string', ['string'])
  const _reset         = wasmModule.cwrap('fl_reset',          null,     [])
  const _version       = wasmModule.cwrap('fl_version',        'string', [])

  return {

    // ── version ─────────────────────────────────
    version() {
      return _version()
    },

    // ── computeLayout ────────────────────────────
    // Takes a JS node tree, returns layout-annotated copy
    //
    // Example:
    //   const layout = engine.computeLayout({
    //     id: 1, tag: 'div',
    //     style: { width: 600, height: 400, flexDirection: 1 },
    //     children: [
    //       { id: 2, tag: 'div', style: { flexGrow: 1 }, children: [] },
    //       { id: 3, tag: 'div', style: { width: 200 },  children: [] },
    //     ]
    //   }, 1280, 800)
    //
    //   // layout.children[0].computed = { x, y, width, height }
    computeLayout(tree, containerWidth = window.innerWidth, containerHeight = window.innerHeight) {
      const input = JSON.stringify({
        width: containerWidth,
        height: containerHeight,
        tree,
      })

      const resultJson = _computeLayout(input)
      const result = JSON.parse(resultJson)

      if (result.error) throw new Error(`FastLayout: ${result.error}`)
      return result
    },

    // ── diff ─────────────────────────────────────
    // Diffs newTree against the last computeLayout tree.
    // Returns minimal patch list.
    //
    // Example:
    //   const patches = engine.diff(updatedTree)
    //   patches.forEach(patch => applyPatch(patch))
    diff(newTree) {
      const input = JSON.stringify({ newTree })
      const resultJson = _diff(input)
      const result = JSON.parse(resultJson)

      if (result && result.error) throw new Error(`FastLayout diff: ${result.error}`)
      return Array.isArray(result) ? result : []
    },

    // ── reset ────────────────────────────────────
    // Call when unmounting / starting fresh
    reset() {
      _nextId = 1
      _reset()
    },

    // ── applyPatches ─────────────────────────────
    // Convenience: apply a patch list to real DOM
    // Pass your own DOM root element
    applyPatches(patches, domRoot, nodeMap) {
      for (const patch of patches) {
        applyPatch(patch, domRoot, nodeMap)
      }
    },
  }
}

// ── DOM patch applier ─────────────────────────
// nodeMap: Map<nodeId, HTMLElement>
function applyPatch(patch, root, nodeMap) {
  const { op, nodeId, parentId, index, newNode, attrs, text } = patch

  switch (op) {
    case PatchOp.Insert: {
      const el = createDOMNode(newNode, nodeMap)
      const parent = nodeMap.get(parentId) || root
      const sibling = parent.children[index]
      parent.insertBefore(el, sibling || null)
      break
    }
    case PatchOp.Remove: {
      const el = nodeMap.get(nodeId)
      if (el) { el.parentNode?.removeChild(el); nodeMap.delete(nodeId) }
      break
    }
    case PatchOp.Replace: {
      const old = nodeMap.get(nodeId)
      const el  = createDOMNode(newNode, nodeMap)
      old?.parentNode?.replaceChild(el, old)
      nodeMap.delete(nodeId)
      break
    }
    case PatchOp.UpdateAttrs: {
      const el = nodeMap.get(nodeId)
      if (!el) break
      for (const [k, v] of Object.entries(attrs)) {
        if (v === '') el.removeAttribute(k)
        else el.setAttribute(k, v)
      }
      break
    }
    case PatchOp.UpdateText: {
      const el = nodeMap.get(nodeId)
      if (el) el.textContent = text
      break
    }
    case PatchOp.Move: {
      const el     = nodeMap.get(nodeId)
      const parent = nodeMap.get(parentId) || root
      const sibling = parent.children[index]
      if (el) parent.insertBefore(el, sibling || null)
      break
    }
  }
}

function createDOMNode(node, nodeMap) {
  const el = document.createElement(node.tag === 'text' ? 'span' : node.tag)
  el.dataset.flId = node.id
  if (node.text) el.textContent = node.text
  for (const [k, v] of Object.entries(node.attrs || {})) el.setAttribute(k, v)
  nodeMap.set(node.id, el)
  for (const child of node.children || []) {
    el.appendChild(createDOMNode(child, nodeMap))
  }
  return el
}

// ── Fallback mock for Node.js testing ─────────
function _createMockEngine() {
  console.warn('[FastLayout] Running in mock mode (no WASM). For testing only.')
  return {
    cwrap: (name) => {
      if (name === 'fl_version')        return () => 'FastLayout MOCK'
      if (name === 'fl_compute_layout') return (json) => {
        const { tree } = JSON.parse(json)
        _mockLayout(tree, 0, 0)
        return JSON.stringify(tree)
      }
      if (name === 'fl_diff')  return () => '[]'
      if (name === 'fl_reset') return () => {}
      return () => null
    }
  }
}

function _mockLayout(node, x, y) {
  node.x = x; node.y = y
  node.width  = node.style?.width  || 100
  node.height = node.style?.height || 40
  let cx = x + (node.style?.paddingLeft || 0)
  let cy = y + (node.style?.paddingTop  || 0)
  for (const child of node.children || []) {
    _mockLayout(child, cx, cy)
    cx += child.width
  }
}