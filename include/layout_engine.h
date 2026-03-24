#pragma once
#include "types.h"
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────
//  FastLayout — Flexbox Layout Solver
//
//  Implements a single-pass flex layout algorithm.
//  Given a root Node with styles set, it computes
//  x, y, width, height for every node in the tree.
//
//  How it works:
//    1. resolveSize()  — determine available space
//    2. layoutNode()   — recursively lay out children
//    3. placeChildren()— position children along main/cross axis
// ─────────────────────────────────────────────

namespace fl {

class LayoutEngine {
public:

    // ── Entry point: lay out a full tree ─────
    void compute(Node& root, float containerWidth, float containerHeight) {
        resolveNode(root, containerWidth, containerHeight);
    }

private:

    // ── Resolve one node and all its children ─
    void resolveNode(Node& node, float availW, float availH) {
        Style& s = node.style;

        // 1. Determine this node's own size
        float nodeW = (s.width  >= 0) ? s.width  : availW;
        float nodeH = (s.height >= 0) ? s.height : availH;

        // Clamp to min/max
        nodeW = std::clamp(nodeW, s.minWidth,  s.maxWidth);
        nodeH = std::clamp(nodeH, s.minHeight, s.maxHeight);

        // Apply aspect ratio if set (width takes priority)
        if (s.aspectRatio > 0 && s.width >= 0 && s.height < 0) {
            nodeH = nodeW / s.aspectRatio;
        }

        node.computed.width  = nodeW;
        node.computed.height = nodeH;

        if (node.children.empty()) return;

        // 2. Inner area (subtract padding)
        float innerW = nodeW - s.paddingLeft - s.paddingRight;
        float innerH = nodeH - s.paddingTop  - s.paddingBottom;

        bool isRow = (s.flexDirection == FlexDirection::Row ||
                      s.flexDirection == FlexDirection::RowReverse);

        // 3. First pass: resolve flex children sizes
        resolveFlexChildren(node, innerW, innerH, isRow);

        // 4. Second pass: position all children
        placeChildren(node, innerW, innerH, isRow);
    }

    // ── Resolve sizes for flex children ──────
    void resolveFlexChildren(Node& node, float innerW, float innerH, bool isRow) {
        Style& ps = node.style;
        float mainSize  = isRow ? innerW : innerH;
        float crossSize = isRow ? innerH : innerW;

        // Collect flex items and fixed items
        float usedMain  = 0.0f;
        float totalGrow = 0.0f;

        for (auto& child : node.children) {
            Style& cs = child->style;

            // Determine base size on main axis
            float base = isRow
                ? (cs.flexBasis >= 0 ? cs.flexBasis : (cs.width  >= 0 ? cs.width  : -1))
                : (cs.flexBasis >= 0 ? cs.flexBasis : (cs.height >= 0 ? cs.height : -1));

            if (base < 0) base = 0.0f; // auto → 0 base

            // Account for margins on the main axis
            float mainMargin = isRow
                ? (cs.marginLeft + cs.marginRight)
                : (cs.marginTop  + cs.marginBottom);

            usedMain  += base + mainMargin + ps.gap;
            totalGrow += cs.flexGrow;

            // Set provisional size on cross axis
            if (isRow) {
                child->computed.width  = base;
                child->computed.height = (cs.height >= 0) ? cs.height : crossSize
                    - cs.marginTop - cs.marginBottom;
            } else {
                child->computed.height = base;
                child->computed.width  = (cs.width >= 0) ? cs.width : crossSize
                    - cs.marginLeft - cs.marginRight;
            }
        }

        // Remove last gap
        if (!node.children.empty()) usedMain -= ps.gap;

        // Distribute remaining space to flex-grow children
        float remaining = mainSize - usedMain;
        if (remaining > 0 && totalGrow > 0) {
            for (auto& child : node.children) {
                if (child->style.flexGrow <= 0) continue;
                float share = remaining * (child->style.flexGrow / totalGrow);
                if (isRow) child->computed.width  += share;
                else       child->computed.height += share;
            }
        }

        // Clamp all children and recurse
        for (auto& child : node.children) {
            Style& cs = child->style;
            child->computed.width  = std::clamp(child->computed.width,  cs.minWidth,  cs.maxWidth);
            child->computed.height = std::clamp(child->computed.height, cs.minHeight, cs.maxHeight);

            // Recurse into this child with its computed size
            float childAvailW = child->computed.width  - cs.paddingLeft - cs.paddingRight;
            float childAvailH = child->computed.height - cs.paddingTop  - cs.paddingBottom;
            if (!child->children.empty()) {
                bool childIsRow = (cs.flexDirection == FlexDirection::Row ||
                                   cs.flexDirection == FlexDirection::RowReverse);
                resolveFlexChildren(*child, childAvailW, childAvailH, childIsRow);
            }
        }
    }

    // ── Position children along axes ─────────
    void placeChildren(Node& node, float innerW, float innerH, bool isRow) {
        Style& ps = node.style;

        float mainSize  = isRow ? innerW : innerH;
        float crossSize = isRow ? innerH : innerW;

        // Compute total children main-axis size (for justification)
        float totalMain = 0.0f;
        for (auto& child : node.children) {
            float mainMargin = isRow
                ? child->style.marginLeft + child->style.marginRight
                : child->style.marginTop  + child->style.marginBottom;
            totalMain += (isRow ? child->computed.width : child->computed.height) + mainMargin;
        }
        if (!node.children.empty()) totalMain += ps.gap * (node.children.size() - 1);

        // Determine starting position for justify-content
        float cursor  = isRow ? ps.paddingLeft : ps.paddingTop;
        float spacing = 0.0f;

        float free = mainSize - totalMain;
        switch (ps.justifyContent) {
            case JustifyContent::End:         cursor  += free; break;
            case JustifyContent::Center:      cursor  += free / 2.0f; break;
            case JustifyContent::SpaceBetween:
                if (node.children.size() > 1)
                    spacing = free / (float)(node.children.size() - 1);
                break;
            case JustifyContent::SpaceAround:
                spacing = free / (float)node.children.size();
                cursor += spacing / 2.0f;
                break;
            case JustifyContent::SpaceEvenly:
                spacing = free / (float)(node.children.size() + 1);
                cursor += spacing;
                break;
            default: break;
        }

        // Reverse direction
        bool reversed = (ps.flexDirection == FlexDirection::RowReverse ||
                         ps.flexDirection == FlexDirection::ColumnReverse);

        if (reversed) {
            cursor = (isRow ? node.computed.width  - ps.paddingRight
                            : node.computed.height - ps.paddingBottom);
        }

        for (auto& child : node.children) {
            Style& cs = child->style;

            float childMain  = isRow ? child->computed.width  : child->computed.height;
            float childCross = isRow ? child->computed.height : child->computed.width;
            float mainMarginStart  = isRow ? cs.marginLeft : cs.marginTop;
            float mainMarginEnd    = isRow ? cs.marginRight: cs.marginBottom;
            float crossMarginStart = isRow ? cs.marginTop  : cs.marginLeft;

            if (reversed) cursor -= childMain + mainMarginEnd;

            // Main axis position
            float mainPos = cursor + (reversed ? 0 : mainMarginStart);

            // Cross axis position (align-items)
            float crossPos = isRow ? ps.paddingTop : ps.paddingLeft;
            crossPos += crossMarginStart;
            switch (ps.alignItems) {
                case AlignItems::End:     crossPos += crossSize - childCross - crossMarginStart; break;
                case AlignItems::Center:  crossPos += (crossSize - childCross) / 2.0f; break;
                case AlignItems::Stretch: /* already full cross size */ break;
                default: break;
            }

            if (isRow) {
                child->computed.x = node.computed.x + mainPos;
                child->computed.y = node.computed.y + crossPos;
            } else {
                child->computed.x = node.computed.x + crossPos;
                child->computed.y = node.computed.y + mainPos;
            }

            if (!reversed) {
                cursor += childMain + mainMarginStart + mainMarginEnd + ps.gap + spacing;
            } else {
                cursor -= mainMarginStart + ps.gap + spacing;
            }

            // Recurse to position grandchildren
            if (!child->children.empty()) {
                bool childIsRow = (cs.flexDirection == FlexDirection::Row ||
                                   cs.flexDirection == FlexDirection::RowReverse);
                float childInnerW = child->computed.width  - cs.paddingLeft - cs.paddingRight;
                float childInnerH = child->computed.height - cs.paddingTop  - cs.paddingBottom;
                placeChildren(*child, childInnerW, childInnerH, childIsRow);
            }
        }
    }
};

} // namespace fl