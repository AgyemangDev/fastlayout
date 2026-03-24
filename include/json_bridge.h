#pragma once
#include "types.h"
#include <string>
#include <sstream>
#include <stdexcept>

// ─────────────────────────────────────────────
//  FastLayout — JSON Bridge
//
//  JS sends us JSON strings; we parse them into
//  C++ Node trees. We send back JSON strings of
//  layout results and patch lists.
//
//  We use a hand-rolled parser here (no deps)
//  keeping the WASM binary lean and fast.
// ─────────────────────────────────────────────

namespace fl {

// ══════════════════════════════════════════════
//  SERIALIZER — C++ → JSON string
// ══════════════════════════════════════════════

class Serializer {
public:

    // ── Serialize layout result tree to JSON ─
    static std::string layoutToJson(const Node& node) {
        std::ostringstream out;
        serializeNode(node, out);
        return out.str();
    }

    // ── Serialize patch list to JSON ──────────
    static std::string patchesToJson(const std::vector<Patch>& patches) {
        std::ostringstream out;
        out << "[";
        for (size_t i = 0; i < patches.size(); ++i) {
            if (i > 0) out << ",";
            serializePatch(patches[i], out);
        }
        out << "]";
        return out.str();
    }

private:
    static void serializeNode(const Node& n, std::ostringstream& out) {
        out << "{"
            << "\"id\":"     << n.id               << ","
            << "\"tag\":\""  << escapeStr(n.tag)    << "\","
            << "\"x\":"      << n.computed.x        << ","
            << "\"y\":"      << n.computed.y        << ","
            << "\"width\":"  << n.computed.width     << ","
            << "\"height\":" << n.computed.height    << ","
            << "\"children\":[";

        for (size_t i = 0; i < n.children.size(); ++i) {
            if (i > 0) out << ",";
            serializeNode(*n.children[i], out);
        }
        out << "]}";
    }

    static void serializePatch(const Patch& p, std::ostringstream& out) {
        out << "{"
            << "\"op\":"      << (int)p.op      << ","
            << "\"nodeId\":"  << p.nodeId        << ","
            << "\"parentId\":" << p.parentId     << ","
            << "\"index\":"   << p.index;

        if (!p.text.empty()) {
            out << ",\"text\":\"" << escapeStr(p.text) << "\"";
        }

        if (!p.attrs.empty()) {
            out << ",\"attrs\":{";
            bool first = true;
            for (auto& [k,v] : p.attrs) {
                if (!first) out << ",";
                out << "\"" << escapeStr(k) << "\":\"" << escapeStr(v) << "\"";
                first = false;
            }
            out << "}";
        }

        if (p.newNode) {
            out << ",\"newNode\":";
            serializeNode(*p.newNode, out);
        }

        out << "}";
    }

    static std::string escapeStr(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += c;
            }
        }
        return out;
    }
};

// ══════════════════════════════════════════════
//  PARSER — JSON string → C++ Node tree
//  A minimal recursive-descent parser
// ══════════════════════════════════════════════

class Parser {
public:
    explicit Parser(const std::string& json) : src_(json), pos_(0) {}

    std::shared_ptr<Node> parseNode() {
        skipWS();
        expect('{');
        auto node = std::make_shared<Node>();

        while (true) {
            skipWS();
            if (peek() == '}') { advance(); break; }

            std::string key = parseString();
            skipWS();
            expect(':');
            skipWS();

            if      (key == "id")       node->id   = (uint32_t)parseInt();
            else if (key == "tag")      node->tag  = parseString();
            else if (key == "text")     node->text = parseString();
            else if (key == "attrs")    node->attrs = parseStringMap();
            else if (key == "style")    node->style = parseStyle();
            else if (key == "children") node->children = parseChildren();
            else skipValue();

            skipWS();
            if (peek() == ',') advance();
        }
        return node;
    }

private:
    const std::string& src_;
    size_t pos_;

    char peek()    { skipWS(); return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char advance() { return src_[pos_++]; }
    void skipWS()  { while (pos_ < src_.size() && std::isspace(src_[pos_])) ++pos_; }

    void expect(char c) {
        skipWS();
        if (pos_ >= src_.size() || src_[pos_] != c)
            throw std::runtime_error(std::string("Expected '") + c + "' at pos " + std::to_string(pos_));
        ++pos_;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (pos_ < src_.size() && src_[pos_] != '"') {
            if (src_[pos_] == '\\') {
                ++pos_;
                switch (src_[pos_]) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    default:   out += src_[pos_];
                }
            } else {
                out += src_[pos_];
            }
            ++pos_;
        }
        expect('"');
        return out;
    }

    long long parseInt() {
        skipWS();
        bool neg = (src_[pos_] == '-');
        if (neg) ++pos_;
        long long v = 0;
        while (pos_ < src_.size() && std::isdigit(src_[pos_]))
            v = v * 10 + (src_[pos_++] - '0');
        return neg ? -v : v;
    }

    float parseFloat() {
        skipWS();
        size_t start = pos_;
        if (src_[pos_] == '-') ++pos_;
        while (pos_ < src_.size() && (std::isdigit(src_[pos_]) || src_[pos_] == '.')) ++pos_;
        return std::stof(src_.substr(start, pos_ - start));
    }

    std::unordered_map<std::string, std::string> parseStringMap() {
        std::unordered_map<std::string, std::string> m;
        expect('{');
        while (peek() != '}') {
            std::string k = parseString();
            skipWS(); expect(':');
            std::string v = parseString();
            m[k] = v;
            skipWS();
            if (peek() == ',') advance();
        }
        expect('}');
        return m;
    }

    Style parseStyle() {
        Style s;
        expect('{');
        while (peek() != '}') {
            std::string k = parseString();
            skipWS(); expect(':');
            skipWS();

            // Dimensions
            if      (k == "width")       s.width       = parseFloat();
            else if (k == "height")      s.height      = parseFloat();
            else if (k == "minWidth")    s.minWidth    = parseFloat();
            else if (k == "maxWidth")    s.maxWidth    = parseFloat();
            else if (k == "minHeight")   s.minHeight   = parseFloat();
            else if (k == "maxHeight")   s.maxHeight   = parseFloat();
            else if (k == "flexGrow")    s.flexGrow    = parseFloat();
            else if (k == "flexShrink")  s.flexShrink  = parseFloat();
            else if (k == "flexBasis")   s.flexBasis   = parseFloat();
            else if (k == "aspectRatio") s.aspectRatio = parseFloat();
            // Padding
            else if (k == "paddingTop")    s.paddingTop    = parseFloat();
            else if (k == "paddingRight")  s.paddingRight  = parseFloat();
            else if (k == "paddingBottom") s.paddingBottom = parseFloat();
            else if (k == "paddingLeft")   s.paddingLeft   = parseFloat();
            // Margin
            else if (k == "marginTop")    s.marginTop    = parseFloat();
            else if (k == "marginRight")  s.marginRight  = parseFloat();
            else if (k == "marginBottom") s.marginBottom = parseFloat();
            else if (k == "marginLeft")   s.marginLeft   = parseFloat();
            else if (k == "gap")          s.gap          = parseFloat();
            // Enums as integers
            else if (k == "flexDirection")  s.flexDirection  = (FlexDirection)parseInt();
            else if (k == "justifyContent") s.justifyContent = (JustifyContent)parseInt();
            else if (k == "alignItems")     s.alignItems     = (AlignItems)parseInt();
            else if (k == "flexWrap")       s.flexWrap       = (FlexWrap)parseInt();
            else if (k == "position")       s.position       = (PositionType)parseInt();
            else if (k == "overflow")       s.overflow       = (Overflow)parseInt();
            else skipValue();

            skipWS();
            if (peek() == ',') advance();
        }
        expect('}');
        return s;
    }

    std::vector<std::shared_ptr<Node>> parseChildren() {
        std::vector<std::shared_ptr<Node>> children;
        expect('[');
        while (peek() != ']') {
            children.push_back(parseNode());
            skipWS();
            if (peek() == ',') advance();
        }
        expect(']');
        return children;
    }

    void skipValue() {
        skipWS();
        char c = src_[pos_];
        if (c == '"') { parseString(); return; }
        if (c == '{') {
            expect('{');
            int depth = 1;
            while (pos_ < src_.size() && depth > 0) {
                if (src_[pos_] == '{') depth++;
                else if (src_[pos_] == '}') depth--;
                ++pos_;
            }
            return;
        }
        if (c == '[') {
            expect('[');
            int depth = 1;
            while (pos_ < src_.size() && depth > 0) {
                if (src_[pos_] == '[') depth++;
                else if (src_[pos_] == ']') depth--;
                ++pos_;
            }
            return;
        }
        // Number, bool, null
        while (pos_ < src_.size() && src_[pos_] != ',' && src_[pos_] != '}' && src_[pos_] != ']')
            ++pos_;
    }
};

} // namespace fl