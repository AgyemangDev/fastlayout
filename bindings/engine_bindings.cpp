#include "../include/types.h"
#include "../include/layout_engine.h"
#include "../include/differ.h"
#include "../include/json_bridge.h"
#include <string>
#include <memory>
#include <unordered_map>

// ─────────────────────────────────────────────
//  FastLayout — Emscripten WASM Bindings
//
//  These are the functions JS calls directly.
//  Each one takes/returns a plain JSON string
//  so the JS developer never touches C++ types.
//
//  Compile with:
//    emcc bindings/engine_bindings.cpp \
//      -I include \
//      -o js/fastlayout.js \
//      -s MODULARIZE=1 \
//      -s EXPORT_NAME="FastLayoutEngine" \
//      -s EXPORTED_FUNCTIONS='["_fl_compute_layout","_fl_diff","_fl_version","_malloc","_free"]' \
//      -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8"]' \
//      -s ALLOW_MEMORY_GROWTH=1 \
//      -s ENVIRONMENT="web,worker" \
//      -O3 \
//      --bind
// ─────────────────────────────────────────────

#ifdef __EMSCRIPTEN__
  #include <emscripten.h>
  #define EXPORT EMSCRIPTEN_KEEPALIVE
#else
  #define EXPORT
#endif

// ── Session state (persists across calls) ────
// We keep the last computed tree so the differ
// can compare against it without re-parsing
static std::shared_ptr<fl::Node> g_lastTree = nullptr;
static fl::LayoutEngine          g_layoutEngine;
static fl::Differ                g_differ;

extern "C" {

// ── fl_version ───────────────────────────────
// Returns engine version string
// JS: const v = Module.ccall('fl_version', 'string', [], [])
EXPORT const char* fl_version() {
    return "FastLayout 1.0.0 (C++/WASM)";
}

// ── fl_compute_layout ─────────────────────────
// Takes a JSON node tree + container dimensions
// Returns JSON with computed x/y/width/height
// for every node in the tree
//
// Input JSON:  { "tree": <NodeJSON>, "width": 1280, "height": 800 }
// Output JSON: <NodeJSON with computed fields>
EXPORT const char* fl_compute_layout(const char* inputJson) {
    static std::string result;

    try {
        // Parse the wrapper object
        std::string input(inputJson);

        // Extract width, height, and tree (simple key search)
        float containerW = 1280.0f;
        float containerH = 800.0f;

        auto extractFloat = [&](const std::string& key) -> float {
            size_t pos = input.find("\"" + key + "\"");
            if (pos == std::string::npos) return -1.0f;
            pos = input.find(':', pos) + 1;
            while (std::isspace(input[pos])) ++pos;
            size_t end = pos;
            while (end < input.size() && (std::isdigit(input[end]) || input[end] == '.' || input[end] == '-')) ++end;
            return std::stof(input.substr(pos, end - pos));
        };

        float w = extractFloat("width");
        float h = extractFloat("height");
        if (w > 0) containerW = w;
        if (h > 0) containerH = h;

        // Find the "tree" value
        size_t treePos = input.find("\"tree\"");
        if (treePos == std::string::npos) {
            result = "{\"error\":\"Missing 'tree' key\"}";
            return result.c_str();
        }
        treePos = input.find(':', treePos) + 1;
        while (std::isspace(input[treePos])) ++treePos;

        fl::Parser parser(input.substr(treePos));
        auto tree = parser.parseNode();

        // Run layout
        g_layoutEngine.compute(*tree, containerW, containerH);

        // Save tree for next diff call
        g_lastTree = tree;

        // Serialize result
        result = fl::Serializer::layoutToJson(*tree);
        return result.c_str();

    } catch (const std::exception& e) {
        result = std::string("{\"error\":\"") + e.what() + "\"}";
        return result.c_str();
    }
}

// ── fl_diff ───────────────────────────────────
// Diffs the current tree against a new one.
// Returns JSON patch list.
//
// Input JSON:  { "newTree": <NodeJSON> }
// Output JSON: [ { "op": 0, "nodeId": 1, ... }, ... ]
//
// Patch ops:
//   0 = Insert
//   1 = Remove
//   2 = Replace
//   3 = UpdateAttrs
//   4 = UpdateText
//   5 = Move
EXPORT const char* fl_diff(const char* inputJson) {
    static std::string result;

    try {
        if (!g_lastTree) {
            result = "{\"error\":\"No previous tree. Call fl_compute_layout first.\"}";
            return result.c_str();
        }

        std::string input(inputJson);

        // Find "newTree" value
        size_t pos = input.find("\"newTree\"");
        if (pos == std::string::npos) {
            result = "{\"error\":\"Missing 'newTree' key\"}";
            return result.c_str();
        }
        pos = input.find(':', pos) + 1;
        while (std::isspace(input[pos])) ++pos;

        fl::Parser parser(input.substr(pos));
        auto newTree = parser.parseNode();

        // Run diff
        auto patches = g_differ.diff(g_lastTree, newTree);

        // Update stored tree
        g_lastTree = newTree;

        result = fl::Serializer::patchesToJson(patches);
        return result.c_str();

    } catch (const std::exception& e) {
        result = std::string("{\"error\":\"") + e.what() + "\"}";
        return result.c_str();
    }
}

// ── fl_reset ──────────────────────────────────
// Clears stored tree state (call when unmounting)
EXPORT void fl_reset() {
    g_lastTree = nullptr;
}

} // extern "C"