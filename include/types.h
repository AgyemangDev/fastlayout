#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cstdint>

// Fast layout engine types
// Every strucure here is designed to be serializable from/to JSON for the JS bridge

namespace fl {

// ── Flex direction ────────────────────────────
enum class FlexDirection { Row, Column, RowReverse, ColumnReverse };
enum class JustifyContent { Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly };
enum class AlignItems    { Start, End, Center, Stretch, Baseline };
enum class FlexWrap      { NoWrap, Wrap, WrapReverse };
enum class PositionType  { Relative, Absolute };
enum class Overflow      { Visible, Hidden, Scroll };


struct Style {
    // Dimensions
    float width          = -1.0f;   // -1 = auto
    float height         = -1.0f;
    float minWidth       = 0.0f;
    float maxWidth       = 1e9f;
    float minHeight      = 0.0f;
    float maxHeight      = 1e9f;
    float flexGrow       = 0.0f;
    float flexShrink     = 1.0f;
    float flexBasis      = -1.0f;   // -1 = auto
    float aspectRatio    = -1.0f;   // -1 = not set
 
    // Spacing
    float paddingTop     = 0.0f;
    float paddingRight   = 0.0f;
    float paddingBottom  = 0.0f;
    float paddingLeft    = 0.0f;
    float marginTop      = 0.0f;
    float marginRight    = 0.0f;
    float marginBottom   = 0.0f;
    float marginLeft     = 0.0f;
    float gap            = 0.0f;
 
    // Layout mode
    FlexDirection  flexDirection  = FlexDirection::Row;
    JustifyContent justifyContent = JustifyContent::Start;
    AlignItems     alignItems     = AlignItems::Stretch;
    FlexWrap       flexWrap       = FlexWrap::NoWrap;
    PositionType   position       = PositionType::Relative;
    Overflow       overflow       = Overflow::Visible;
 
    // Absolute position anchors
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;
    float left   = 0.0f;
};

    struct Layout {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    // A node in virtual tree
    struct Node {
        uint32_t id; // unique identifier for the node
        std::string tag;
        std::string text;
        Style style;
        std::unordered_map<std::string, std::string> attrs;
        std::vector<std::shared_ptr<Node>> children;


        // Computed by layout engine
        Layout computed;


        Node () : id(0) {}
        explicit Node(uint32_t id, const std::string& tag = "div")
            : id(id), tag(tag) {}
    };
    enum class PatchOp { Insert, Remove, Replace, UpdateAttrs, UpdateText, Move };
 
struct Patch {
    PatchOp     op;
    uint32_t    nodeId;
    uint32_t    parentId;
    int         index;          // position in parent's children
    std::shared_ptr<Node> newNode;  // for Insert / Replace
    std::unordered_map<std::string, std::string> attrs;  // for UpdateAttrs
    std::string text;           // for UpdateText
 
    Patch(PatchOp op, uint32_t nodeId, uint32_t parentId, int index = 0)
        : op(op), nodeId(nodeId), parentId(parentId), index(index) {}
};
};