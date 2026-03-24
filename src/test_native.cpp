// 
// FastLayout = Native test suite
//  Compile:
//    g++ -std=c++17 -I include src/test_native.cpp -o test_native && ./test_native
// ─────────────────────────────────────────────


#include "../include/types.h"
#include "../include/layout_engine.h"
#include "../include/differ.h"
#include "../include/json_bridge.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace fl;

bool approx (float a, float b, float eps = 0.5f){
    return std::abs(a - b) < eps;
}

void pass(const std::string& name) {
    std::cout << "  \033[32m✓\033[0m " << name << "\n";
}
 
void fail(const std::string& name, const std::string& msg) {
    std::cout << "  \033[31m✗\033[0m " << name << ": " << msg << "\n";
}

// ─// ── Test 1: Basic row layout ───────────────────
void test_basic_row() {
    std::cout << "\n[1] Basic row layout\n";
 
    auto root = std::make_shared<Node>(1, "div");
    root->style.width  = 600;
    root->style.height = 100;
    root->style.flexDirection = FlexDirection::Row;
 
    auto child1 = std::make_shared<Node>(2, "div");
    child1->style.flexGrow = 1;
 
    auto child2 = std::make_shared<Node>(3, "div");
    child2->style.flexGrow = 1;
 
    root->children = {child1, child2};
 
    LayoutEngine engine;
    engine.compute(*root, 600, 100);
 
    // Each child should be 300px wide (600/2)
    if (approx(child1->computed.width, 300.0f))
        pass("child1 width = 300");
    else
        fail("child1 width", "got " + std::to_string(child1->computed.width));
 
    if (approx(child2->computed.x, 300.0f))
        pass("child2 x = 300 (placed after child1)");
    else
        fail("child2 x", "got " + std::to_string(child2->computed.x));
}
 
// ── Test 2: Padding + gap ──────────────────────
void test_padding_gap() {
    std::cout << "\n[2] Padding and gap\n";
 
    auto root = std::make_shared<Node>(1, "div");
    root->style.width        = 500;
    root->style.height       = 200;
    root->style.paddingLeft  = 20;
    root->style.paddingRight = 20;
    root->style.gap          = 10;
    root->style.flexDirection = FlexDirection::Row;
 
    // Two equal-grow children
    for (int i = 2; i <= 3; ++i) {
        auto c = std::make_shared<Node>(i, "div");
        c->style.flexGrow = 1;
        root->children.push_back(c);
    }
 
    LayoutEngine engine;
    engine.compute(*root, 500, 200);
 
    // Available = 500 - 20 - 20 - 10 (gap) = 450
    // Each child = 450 / 2 = 225
    float expectedW = (500 - 20 - 20 - 10) / 2.0f;
    if (approx(root->children[0]->computed.width, expectedW))
        pass("child width accounts for padding + gap");
    else
        fail("child width", "expected " + std::to_string(expectedW)
             + " got " + std::to_string(root->children[0]->computed.width));
 
    if (approx(root->children[0]->computed.x, 20.0f))
        pass("child1 x = 20 (padded start)");
    else
        fail("child1 x", "got " + std::to_string(root->children[0]->computed.x));
}
 
// ── Test 3: Nested columns ────────────────────
void test_nested_columns() {
    std::cout << "\n[3] Nested column layout\n";
 
    auto root = std::make_shared<Node>(1, "div");
    root->style.width  = 400;
    root->style.height = 600;
    root->style.flexDirection = FlexDirection::Column;
 
    auto header = std::make_shared<Node>(2, "div");
    header->style.height = 80;
 
    auto body = std::make_shared<Node>(3, "div");
    body->style.flexGrow = 1;   // take remaining space
 
    auto footer = std::make_shared<Node>(4, "div");
    footer->style.height = 60;
 
    root->children = {header, body, footer};
 
    LayoutEngine engine;
    engine.compute(*root, 400, 600);
 
    if (approx(header->computed.y, 0.0f))
        pass("header at y=0");
    if (approx(header->computed.height, 80.0f))
        pass("header height=80");
    if (approx(body->computed.y, 80.0f))
        pass("body placed after header");
    if (approx(body->computed.height, 460.0f))  // 600 - 80 - 60
        pass("body fills remaining space (460px)");
    else
        fail("body height", "expected 460 got " + std::to_string(body->computed.height));
    if (approx(footer->computed.y, 540.0f))     // 80 + 460
        pass("footer at y=540");
}
 
// ── Test 4: Differ detects changes ────────────
void test_differ_attrs() {
    std::cout << "\n[4] Differ — attribute change detection\n";
 
    auto old = std::make_shared<Node>(1, "div");
    old->attrs["class"] = "box";
    old->attrs["data-x"] = "100";
 
    auto newNode = std::make_shared<Node>(1, "div");
    newNode->attrs["class"] = "box highlighted";  // changed
    // data-x removed
 
    Differ differ;
    auto patches = differ.diff(old, newNode);
 
    // Should have one UpdateAttrs patch
    bool hasAttrPatch = false;
    for (auto& p : patches) {
        if (p.op == PatchOp::UpdateAttrs) {
            hasAttrPatch = true;
            if (p.attrs.count("class") && p.attrs.at("class") == "box highlighted")
                pass("class attr update detected");
            if (p.attrs.count("data-x") && p.attrs.at("data-x").empty())
                pass("removed attr marked with empty string");
        }
    }
    if (!hasAttrPatch) fail("UpdateAttrs patch", "not generated");
}
 
// ── Test 5: Differ detects insertions ─────────
void test_differ_insert() {
    std::cout << "\n[5] Differ — child insertion\n";
 
    auto old = std::make_shared<Node>(1, "div");
    auto child1 = std::make_shared<Node>(2, "div"); child1->attrs["k"] = "a";
    old->children = {child1};
 
    auto newRoot = std::make_shared<Node>(1, "div");
    auto nChild1 = std::make_shared<Node>(2, "div"); nChild1->attrs["k"] = "a";
    auto nChild2 = std::make_shared<Node>(3, "div"); nChild2->attrs["k"] = "b"; // new
    newRoot->children = {nChild1, nChild2};
 
    Differ differ;
    auto patches = differ.diff(old, newRoot);
 
    bool hasInsert = false;
    for (auto& p : patches) {
        if (p.op == PatchOp::Insert && p.nodeId == 3) hasInsert = true;
    }
    if (hasInsert) pass("Insert patch generated for new child id=3");
    else           fail("Insert patch", "not generated");
}
 
// ── Test 6: JSON round-trip ────────────────────
void test_json_roundtrip() {
    std::cout << "\n[6] JSON serialization round-trip\n";
 
    auto node = std::make_shared<Node>(42, "section");
    node->style.width  = 800;
    node->style.height = 400;
    node->attrs["id"]  = "main";
 
    auto child = std::make_shared<Node>(43, "p");
    child->text = "Hello, world!";
    node->children = {child};
 
    // Serialize
    std::string json = Serializer::layoutToJson(*node);
 
    // Should contain key fields
    if (json.find("\"id\":42")      != std::string::npos) pass("id=42 in JSON");
    if (json.find("\"tag\":\"section\"") != std::string::npos) pass("tag=section in JSON");
    if (json.find("\"width\":800")  != std::string::npos) pass("width=800 in JSON");
    if (json.find("children")       != std::string::npos) pass("children array in JSON");
 
    std::cout << "  JSON output: " << json.substr(0, 120) << "...\n";
}
 
// ─────────────────────────────────────────────
int main() {
    std::cout << "═══════════════════════════════════\n";
    std::cout << "  FastLayout Engine — Native Tests\n";
    std::cout << "═══════════════════════════════════\n";
 
    test_basic_row();
    test_padding_gap();
    test_nested_columns();
    test_differ_attrs();
    test_differ_insert();
    test_json_roundtrip();
 
    std::cout << "\n✅ All tests complete.\n\n";
    return 0;
}
 