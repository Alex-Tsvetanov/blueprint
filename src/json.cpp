#include "blueprint/json.hpp"

#include <cmath>
#include <cstdio>

namespace bp {
namespace {

const Json& null_json() {
    static const Json instance;
    return instance;
}

// Clang's dump of a large translation unit nests expression nodes deeply. The
// limit is a guard against a runaway input, not a real structural bound; it is
// far above anything the frontend produces for a declaration-only header.
constexpr int kMaxDepth = 4096;

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    Json parse() {
        skip_ws();
        Json v = value(0);
        skip_ws();
        if (i_ != s_.size()) fail("trailing characters after top-level value");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        // Byte offset, not line and column: the input is machine written and
        // the offset is what a hex dump needs.
        throw JsonError("JSON at byte " + std::to_string(i_) + ": " + what);
    }

    bool eof() const { return i_ >= s_.size(); }
    char peek() const { return eof() ? '\0' : s_[i_]; }

    void skip_ws() {
        while (!eof()) {
            const char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++i_;
    }

    Json value(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        skip_ws();
        if (eof()) fail("unexpected end of input");
        switch (peek()) {
        case '{': return object(depth);
        case '[': return array(depth);
        case '"': return Json(string());
        case 't': literal("true"); return Json(true);
        case 'f': literal("false"); return Json(false);
        case 'n': literal("null"); return Json();
        default: return number();
        }
    }

    void literal(std::string_view word) {
        if (s_.substr(i_, word.size()) != word) fail("bad literal");
        i_ += word.size();
    }

    Json object(int depth) {
        expect('{');
        Json obj = Json::object();
        skip_ws();
        if (peek() == '}') { ++i_; return obj; }
        for (;;) {
            skip_ws();
            std::string key = string();
            skip_ws();
            expect(':');
            obj.set(std::move(key), value(depth + 1));
            skip_ws();
            if (peek() == ',') { ++i_; continue; }
            expect('}');
            return obj;
        }
    }

    Json array(int depth) {
        expect('[');
        Json arr = Json::array();
        skip_ws();
        if (peek() == ']') { ++i_; return arr; }
        for (;;) {
            arr.push_back(value(depth + 1));
            skip_ws();
            if (peek() == ',') { ++i_; continue; }
            expect(']');
            return arr;
        }
    }

    // Encodes one code point as UTF-8. A surrogate pair is joined by the
    // caller before this is reached.
    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    unsigned hex4() {
        if (i_ + 4 > s_.size()) fail("truncated unicode escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("bad hex digit in unicode escape");
        }
        return v;
    }

    std::string string() {
        expect('"');
        std::string out;
        for (;;) {
            if (eof()) fail("unterminated string");
            const char c = s_[i_++];
            if (c == '"') return out;
            if (c != '\\') { out.push_back(c); continue; }
            if (eof()) fail("unterminated escape");
            const char e = s_[i_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() &&
                    s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                    i_ += 2;
                    const unsigned low = hex4();
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        append_utf8(out, cp);
                        cp = low;
                    }
                }
                append_utf8(out, cp);
                break;
            }
            default: fail("unknown escape");
            }
        }
    }

    Json number() {
        const std::size_t start = i_;
        if (peek() == '-') ++i_;
        while (!eof() && ((peek() >= '0' && peek() <= '9') || peek() == '+' ||
                          peek() == '-' || peek() == '.' || peek() == 'e' || peek() == 'E')) {
            ++i_;
        }
        if (start == i_) fail("expected a value");
        const std::string text(s_.substr(start, i_ - start));
        try {
            return Json(std::stod(text));
        } catch (const std::exception&) {
            fail("malformed number");
        }
    }

    std::string_view s_;
    std::size_t i_ = 0;
};

std::string number_to_string(double v) {
    if (v == static_cast<double>(static_cast<long long>(v)) && std::abs(v) < 9.0e15) {
        return std::to_string(static_cast<long long>(v));
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

} // namespace

const std::string& Json::as_string() const {
    static const std::string empty;
    return is_string() ? string_ : empty;
}

const Json* Json::find(std::string_view key) const {
    if (!is_object()) return nullptr;
    for (const auto& m : object_) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}

const Json& Json::at(std::string_view key) const {
    const Json* found = find(key);
    return found ? *found : null_json();
}

void Json::push_back(Json v) {
    if (!is_array()) *this = Json::array();
    array_.push_back(std::move(v));
}

void Json::set(std::string key, Json v) {
    if (!is_object()) *this = Json::object();
    for (auto& m : object_) {
        if (m.first == key) { m.second = std::move(v); return; }
    }
    object_.emplace_back(std::move(key), std::move(v));
}

std::string json_quote(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                // Bytes at or above 0x80 pass through unchanged: the input is
                // already UTF-8 and JSON permits it verbatim.
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
    return out;
}

void Json::dump_into(std::string& out, int indent, int depth) const {
    const bool pretty = indent >= 0;
    const std::string pad =
        pretty ? std::string(static_cast<std::size_t>(indent * (depth + 1)), ' ') : std::string();
    const std::string pad_close =
        pretty ? std::string(static_cast<std::size_t>(indent * depth), ' ') : std::string();
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: out += number_to_string(number_); break;
    case Type::String: out += json_quote(string_); break;
    case Type::Array:
        if (array_.empty()) { out += "[]"; break; }
        out += '[';
        for (std::size_t k = 0; k < array_.size(); ++k) {
            if (k) out += ',';
            if (pretty) { out += '\n'; out += pad; }
            array_[k].dump_into(out, indent, depth + 1);
        }
        if (pretty) { out += '\n'; out += pad_close; }
        out += ']';
        break;
    case Type::Object:
        if (object_.empty()) { out += "{}"; break; }
        out += '{';
        for (std::size_t k = 0; k < object_.size(); ++k) {
            if (k) out += ',';
            if (pretty) { out += '\n'; out += pad; }
            out += json_quote(object_[k].first);
            out += pretty ? ": " : ":";
            object_[k].second.dump_into(out, indent, depth + 1);
        }
        if (pretty) { out += '\n'; out += pad_close; }
        out += '}';
        break;
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_into(out, indent, 0);
    return out;
}

Json Json::parse(std::string_view text) {
    // A UTF-8 byte order mark is accepted and skipped; some editors add one.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    return Parser(text).parse();
}

} // namespace bp
