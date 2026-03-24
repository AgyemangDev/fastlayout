#include "../include/types.h"
#include "../include/layout_engine.h"
#include "../include/differ.h"
#include "../include/json_bridge.h"
#include <string>
#include <memory>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
  #include <emscripten.h>
  #define EXPORT EMSCRIPTEN_KEEPALIVE
#else
  #define EXPORT
#endif

static std::shared_ptr<fl::Node> g_lastTree = nullptr;
static fl::LayoutEngine          g_layoutEngine;
static fl::Differ                g_differ;

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
    int depth = 0;
    bool inString = false, escape = false;
    size_t end = start;
    for (size_t i = start; i < len; ++i) {
        char c = input[i];
        if (escape)              { escape = false; continue; }
        if (c == '\\' && inString) { escape = true; continue; }
        if (c == '"')            { inString = !inString; continue; }
        if (inString)            { continue; }
        if (c == opener)         { ++depth; }
        else if (c == closer)    { --depth; if (depth == 0) { end = i; break; } }
    }
    if (depth != 0) return "";
    return input.substr(start, end - start + 1);
}

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
    while (end < len && (std::isdigit((unsigned char)input[end]) || input[end] == '.' || input[end] == '-')) ++end;
    if (end == start) return -1.0f;
    try { return std::stof(input.substr(start, end - start)); } catch (...) { return -1.0f; }
}

extern "C" {

EXPORT const char* fl_version() {
    return "FastLayout 1.0.0 (C++/WASM)";
}

EXPORT const char* fl_compute_layout(const char* inputJson) {
    static std::string result;
    if (!inputJson) { result = "{\"error\":\"null input\"}"; return result.c_str(); }
    try {
        std::string input(inputJson);
        if (input.empty()) { result = "{\"error\":\"empty input\"}"; return result.c_str(); }
        float containerW = 1280.0f, containerH = 800.0f;
        float w = extractFloat(input, "width");
        float h = extractFloat(input, "height");
        if (w > 0) containerW = w;
        if (h > 0) containerH = h;
        std::string treeJson = extractJsonValue(input, "tree");
        if (treeJson.empty()) { result = "{\"error\":\"Missing or malformed tree\"}"; return result.c_str(); }
        fl::Parser parser(treeJson);
        auto tree = parser.parseNode();
        g_layoutEngine.compute(*tree, containerW, containerH);
        g_lastTree = tree;
        result = fl::Serializer::layoutToJson(*tree);
        return result.c_str();
    } catch (const std::exception& e) {
        result = std::string("{\"error\":\"") + e.what() + "\"}";
        return result.c_str();
    } catch (...) {
        result = "{\"error\":\"unknown exception\"}";
        return result.c_str();
    }
}

EXPORT const char* fl_diff(const char* inputJson) {
    static std::string result;
    if (!inputJson) { result = "{\"error\":\"null input\"}"; return result.c_str(); }
    try {
        if (!g_lastTree) { result = "{\"error\":\"No previous tree\"}"; return result.c_str(); }
        std::string input(inputJson);
        std::string newTreeJson = extractJsonValue(input, "newTree");
        if (newTreeJson.empty()) { result = "{\"error\":\"Missing newTree\"}"; return result.c_str(); }
        fl::Parser parser(newTreeJson);
        auto newTree = parser.parseNode();
        auto patches = g_differ.diff(g_lastTree, newTree);
        g_lastTree = newTree;
        result = fl::Serializer::patchesToJson(patches);
        return result.c_str();
    } catch (const std::exception& e) {
        result = std::string("{\"error\":\"") + e.what() + "\"}";
        return result.c_str();
    } catch (...) {
        result = "{\"error\":\"unknown exception\"}";
        return result.c_str();
    }
}

EXPORT void fl_reset() { g_lastTree = nullptr; }

} // extern "C"