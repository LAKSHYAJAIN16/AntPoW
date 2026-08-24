#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace antchain_node {

// Minimal JSON value type: enough to (a) build the canonical byte-payloads
// that get hashed/signed (mirrors node/antchain_node/crypto.py's
// canonical_json: object keys sorted, compact separators, deterministic
// number formatting), and (b) serialize/parse chain persistence files and
// P2P wire messages. Not a general-purpose JSON library -- no comments, no
// duplicate-key merging beyond last-write-wins, no big-number precision
// beyond int64_t/double.
class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string& msg) : std::runtime_error(msg) {}
};

class Json {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    Json() : type_(Type::Null) {}

    static Json null() { return Json(); }
    static Json boolean(bool b) { Json j; j.type_ = Type::Bool; j.bool_ = b; return j; }
    static Json integer(int64_t i) { Json j; j.type_ = Type::Int; j.int_ = i; return j; }
    static Json number(double d) { Json j; j.type_ = Type::Double; j.double_ = d; return j; }
    static Json string(std::string s) { Json j; j.type_ = Type::String; j.str_ = std::move(s); return j; }
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // ---- array / object mutation ----
    void push_back(Json v) {
        if (type_ != Type::Array) throw JsonError("push_back on non-array Json");
        arr_.push_back(std::move(v));
    }
    void set(const std::string& key, Json v) {
        if (type_ != Type::Object) throw JsonError("set on non-object Json");
        for (auto& kv : obj_) {
            if (kv.first == key) { kv.second = std::move(v); return; }
        }
        obj_.emplace_back(key, std::move(v));
    }

    // ---- accessors ----
    bool as_bool() const { require(Type::Bool); return bool_; }
    int64_t as_int() const {
        if (type_ == Type::Int) return int_;
        if (type_ == Type::Double) return static_cast<int64_t>(double_);
        throw JsonError("Json value is not a number");
    }
    double as_double() const {
        if (type_ == Type::Double) return double_;
        if (type_ == Type::Int) return static_cast<double>(int_);
        throw JsonError("Json value is not a number");
    }
    const std::string& as_string() const { require(Type::String); return str_; }
    const std::vector<Json>& as_array() const { require(Type::Array); return arr_; }
    size_t size() const {
        if (type_ == Type::Array) return arr_.size();
        if (type_ == Type::Object) return obj_.size();
        throw JsonError("size() on non-container Json");
    }

    bool has(const std::string& key) const {
        require(Type::Object);
        for (const auto& kv : obj_) if (kv.first == key) return true;
        return false;
    }
    const Json& at(const std::string& key) const {
        require(Type::Object);
        for (const auto& kv : obj_) if (kv.first == key) return kv.second;
        throw JsonError("missing key '" + key + "'");
    }
    // returns `fallback` if the key is absent (used for optional fields).
    const Json& get(const std::string& key, const Json& fallback) const {
        require(Type::Object);
        for (const auto& kv : obj_) if (kv.first == key) return kv.second;
        return fallback;
    }
    const Json& at(size_t i) const { require(Type::Array); return arr_.at(i); }

    // Canonical form: object keys sorted ascending, no whitespace, matches
    // the byte-payload every hash/signature commits to.
    std::string dump_canonical() const;
    // Same encoding, kept as a separate entry point for wire/persistence
    // use -- both paths currently share one deterministic format.
    std::string dump() const { return dump_canonical(); }

    static Json parse(const std::string& text);

private:
    void require(Type t) const {
        if (type_ != t) throw JsonError("Json type mismatch");
    }
    void dump_to(std::string& out) const;

    Type type_;
    bool bool_ = false;
    int64_t int_ = 0;
    double double_ = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

} // namespace antchain_node
