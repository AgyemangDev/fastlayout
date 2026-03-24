#include "../include/types.h"
#include "../include/layout_engine.h"
#include "../include/differ.h"
#include "../include/json_bridge.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <cstring>
#include <cstdlib>

#ifdef __EMSCRIPTEN__
  #include <emscripten.h>
  #define EXPORT EMSCRIPTEN_KEEPALIVE
#else
  #define EXPORT
#endif

// ─────────────────────────────────────────────
//  Global state
// ─────────────────────────────────────────────
static std::shared_ptr<fl::Node> g_lastTree     = nullptr;
static fl::LayoutEngine          g_layoutEngine;
static fl::Differ                g_differ;

// ─────────────────────────────────────────────
//  Safe result buffer
//  We heap-allocate a persistent buffer so the
//  pointer stays valid after the call returns.
//  JS must call fl_free_result() when done.
//  (Or use the in-place variants below.)
// ─────────────────────────────────────────────
static char*  g_resultBuf  = nullptr;
static size_t g_resultSize = 0;

static const char* storeResult(const std::string& s) {
    size_t needed = s.size() + 1;
    if (needed > g_resultSize) {
        free(g_resultBuf);
        g_resultBuf  = (char*)malloc(needed);
        g_resultSize = needed;
    }
    memcpy(g_resultBuf, s.c_str(), needed);
    return g_resultBuf;
}

// ─────────────────────────────────────────────
//  Minimal JSON helpers  (no regex, no stdlib
//  JSON — keeps binary small)
// ─────────────────────────────────────────────
static float extractFloat(const std::string& input, const std::string& key) {
    size_t pos = input.find("\"" + key + "\"");
    if (pos == std::string::npos) return -1.0f;
    size_t colon = input.find(':', pos);
    if (colon == std::string::npos) return -1.0f;
    size_t start = colon + 1;
    const size_t len = input.size();
    while (start < len && std::isspace((unsigned char)input[start])) ++start;
    if (start >= len) return -1.0f;
    size_t end = start;
    while (end < len && (std::isdigit((unsigned char)input[end]) ||
                         input[end] == '.' || input[end] == '-')) ++end;
    if (end == start) return -1.0f;
    try { return std::stof(input.substr(start, end - start)); }
    catch (...) { return -1.0f; }
}

// Extract top-level value for a key (object or array-aware)
static std::string extractJsonValue(const std::string& input, const std::string& key) {
    const size_t len = input.size();
    size_t keyPos = input.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return "";
    size_t colonPos = input.find(':', keyPos);
    if (colonPos == std::string::npos || colonPos + 1 >= len) return "";
    size_t start = colonPos + 1;
    while (start < len && std::isspace((unsigned char)input[start])) ++start;
    if (start >= len) return "";
    char opener = input[start];
    char closer = (opener == '{') ? '}' : (opener == '[') ? ']' : '\0';
    if (closer == '\0') {
        size_t end = start;
        while (end < len && input[end] != ',' && input[end] != '}' && input[end] != ']') ++end;
        return input.substr(start, end - start);
    }
    int depth = 0; bool inStr = false, esc = false; size_t end = start;
    for (size_t i = start; i < len; ++i) {
        char c = input[i];
        if (esc)              { esc = false; continue; }
        if (c=='\\' && inStr) { esc = true;  continue; }
        if (c == '"')         { inStr = !inStr; continue; }
        if (inStr)            continue;
        if (c == opener)      ++depth;
        else if (c == closer) { --depth; if (!depth) { end = i; break; } }
    }
    if (depth) return "";
    return input.substr(start, end - start + 1);
}

// ─────────────────────────────────────────────
extern "C" {

// ── Version ───────────────────────────────────
EXPORT const char* fl_version() {
    return "FastLayout 1.1.0 (C++17/WASM)";
}

// ── Compute layout ────────────────────────────
//
//  FIX 1: return value via heap buffer (not static
//  local), so the pointer is always valid when JS
//  reads it after the call returns.
//
//  FIX 2: parse the tree once per fl_compute_layout
//  call but cache it for fl_diff.  The benchmark
//  calls this in a tight loop — the bottleneck was
//  always the JSON round-trip, not the solver.
//
EXPORT const char* fl_compute_layout(const char* inputJson) {
    if (!inputJson) return storeResult("{\"error\":\"null input\"}");
    try {
        std::string input(inputJson);
        if (input.empty()) return storeResult("{\"error\":\"empty input\"}");

        float containerW = 1280.0f, containerH = 800.0f;
        float w = extractFloat(input, "width");
        float h = extractFloat(input, "height");
        if (w > 0) containerW = w;
        if (h > 0) containerH = h;

        std::string treeJson = extractJsonValue(input, "tree");
        if (treeJson.empty()) return storeResult("{\"error\":\"Missing or malformed tree\"}");

        fl::Parser parser(treeJson);
        auto tree = parser.parseNode();

        g_layoutEngine.compute(*tree, containerW, containerH);
        g_lastTree = tree;

        return storeResult(fl::Serializer::layoutToJson(*tree));
    } catch (const std::exception& e) {
        return storeResult(std::string("{\"error\":\"") + e.what() + "\"}");
    } catch (...) {
        return storeResult("{\"error\":\"unknown exception\"}");
    }
}

// ── Diff two trees ────────────────────────────
EXPORT const char* fl_diff(const char* inputJson) {
    if (!inputJson) return storeResult("{\"error\":\"null input\"}");
    try {
        if (!g_lastTree) return storeResult("{\"error\":\"No previous tree\"}");
        std::string input(inputJson);
        std::string newTreeJson = extractJsonValue(input, "newTree");
        if (newTreeJson.empty()) return storeResult("{\"error\":\"Missing newTree\"}");

        fl::Parser parser(newTreeJson);
        auto newTree = parser.parseNode();
        auto patches = g_differ.diff(g_lastTree, newTree);
        g_lastTree   = newTree;

        return storeResult(fl::Serializer::patchesToJson(patches));
    } catch (const std::exception& e) {
        return storeResult(std::string("{\"error\":\"") + e.what() + "\"}");
    } catch (...) {
        return storeResult("{\"error\":\"unknown exception\"}");
    }
}

// ── Reset cached state ────────────────────────
EXPORT void fl_reset() { g_lastTree = nullptr; }

// ── Free the result buffer ────────────────────
//  Call this from JS if you want to reclaim the
//  ~2 MB that large results occupy.  Optional —
//  the buffer is reused automatically.
EXPORT void fl_free_result() {
    free(g_resultBuf);
    g_resultBuf  = nullptr;
    g_resultSize = 0;
}

} // extern "C"