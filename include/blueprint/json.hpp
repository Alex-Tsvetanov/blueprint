// Minimal JSON document model, parser and writer.
//
// It exists because the extractor reads the JSON abstract syntax tree that
// Clang prints, and the project takes no third-party dependency. The scope is
// exactly what that job needs: RFC 8259 values, no comments, no trailing
// commas, no duplicate-key merging.
//
// Object members are kept in insertion order in a vector. Lookup is linear,
// which is the right trade here: Clang's nodes hold a handful of keys each,
// and insertion order makes the writer's output stable and diffable.
#ifndef BLUEPRINT_JSON_HPP
#define BLUEPRINT_JSON_HPP

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bp {

class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string& what) : std::runtime_error(what) {}
};

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Member = std::pair<std::string, Json>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : type_(Type::Bool), bool_(v) {}
    Json(double v) : type_(Type::Number), number_(v) {}
    Json(int v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    Json(long long v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    Json(std::size_t v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    Json(const char* v) : type_(Type::String), string_(v) {}
    Json(std::string v) : type_(Type::String), string_(std::move(v)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // Reading. Every accessor is total: a wrong type yields the fallback
    // rather than throwing, because Clang omits keys it considers default and
    // a missing key is the normal case, not an error.
    bool as_bool(bool fallback = false) const { return is_bool() ? bool_ : fallback; }
    double as_number(double fallback = 0.0) const { return is_number() ? number_ : fallback; }
    long long as_int(long long fallback = 0) const {
        return is_number() ? static_cast<long long>(number_) : fallback;
    }
    const std::string& as_string() const;
    std::string as_string_or(std::string fallback) const {
        return is_string() ? string_ : std::move(fallback);
    }

    const std::vector<Json>& items() const { return array_; }
    const std::vector<Member>& members() const { return object_; }

    // Object member lookup; nullptr when absent or when this is not an object.
    const Json* find(std::string_view key) const;
    // Convenience: a null Json when absent, so chains do not need null checks.
    const Json& at(std::string_view key) const;
    std::string str(std::string_view key, std::string fallback = {}) const {
        return at(key).as_string_or(std::move(fallback));
    }
    bool flag(std::string_view key, bool fallback = false) const {
        return at(key).as_bool(fallback);
    }

    // Writing.
    void push_back(Json v);
    void set(std::string key, Json v);

    // indent < 0 emits one line; indent >= 0 pretty-prints with that step.
    std::string dump(int indent = -1) const;

    static Json parse(std::string_view text);

private:
    void dump_into(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::vector<Member> object_;
};

// Escapes a string as a JSON string literal, quotes included.
std::string json_quote(std::string_view s);

} // namespace bp

#endif
