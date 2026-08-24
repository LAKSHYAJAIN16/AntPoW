#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace antchain_node {

namespace {

void dump_string(const std::string& s, std::string& out) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

// %.17g round-trips any double exactly and is deterministic for a fixed
// libc/toolchain -- this project makes no claim of cross-toolchain
// byte-identical hashing, only internal self-consistency (see cpp_node/README.md).
void dump_double(double d, std::string& out) {
    if (std::isnan(d) || std::isinf(d)) throw JsonError("cannot encode non-finite number");
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", d);
    out += buf;
    // Ensure it round-trips as a float, not an int token, when the shortest
    // representation happens to be integral (e.g. 50.0 -> "50").
    if (!std::any_of(buf, buf + std::strlen(buf), [](char c) {
            return c == '.' || c == 'e' || c == 'E' || c == 'n' /*nan*/;
        })) {
        out += ".0";
    }
}

} // namespace

void Json::dump_to(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Int: out += std::to_string(int_); break;
        case Type::Double: dump_double(double_, out); break;
        case Type::String: dump_string(str_, out); break;
        case Type::Array: {
            out.push_back('[');
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out.push_back(',');
                arr_[i].dump_to(out);
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            std::vector<const std::pair<std::string, Json>*> sorted;
            sorted.reserve(obj_.size());
            for (const auto& kv : obj_) sorted.push_back(&kv);
            std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
                return a->first < b->first;
            });
            out.push_back('{');
            for (size_t i = 0; i < sorted.size(); ++i) {
                if (i) out.push_back(',');
                dump_string(sorted[i]->first, out);
                out.push_back(':');
                sorted[i]->second.dump_to(out);
            }
            out.push_back('}');
            break;
        }
    }
}

std::string Json::dump_canonical() const {
    std::string out;
    dump_to(out);
    return out;
}

// ---------------------------------------------------------------- parsing

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text), i_(0) {}

    Json parse_value() {
        skip_ws();
        if (i_ >= s_.size()) throw JsonError("unexpected end of input");
        char c = s_[i_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return Json::string(parse_string());
        if (c == 't') { expect_literal("true"); return Json::boolean(true); }
        if (c == 'f') { expect_literal("false"); return Json::boolean(false); }
        if (c == 'n') { expect_literal("null"); return Json::null(); }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        throw JsonError("unexpected character in JSON input");
    }

    void expect_end() {
        skip_ws();
        if (i_ != s_.size()) throw JsonError("trailing data after JSON value");
    }

private:
    const std::string& s_;
    size_t i_;

    void skip_ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    void expect_literal(const char* lit) {
        size_t n = std::strlen(lit);
        if (s_.compare(i_, n, lit) != 0) throw JsonError("invalid literal in JSON input");
        i_ += n;
    }

    char peek() const {
        if (i_ >= s_.size()) throw JsonError("unexpected end of input");
        return s_[i_];
    }

    Json parse_object() {
        Json obj = Json::object();
        ++i_; // '{'
        skip_ws();
        if (peek() == '}') { ++i_; return obj; }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            if (peek() != ':') throw JsonError("expected ':' in object");
            ++i_;
            Json val = parse_value();
            obj.set(key, std::move(val));
            skip_ws();
            char c = peek();
            if (c == ',') { ++i_; continue; }
            if (c == '}') { ++i_; break; }
            throw JsonError("expected ',' or '}' in object");
        }
        return obj;
    }

    Json parse_array() {
        Json arr = Json::array();
        ++i_; // '['
        skip_ws();
        if (peek() == ']') { ++i_; return arr; }
        while (true) {
            arr.push_back(parse_value());
            skip_ws();
            char c = peek();
            if (c == ',') { ++i_; continue; }
            if (c == ']') { ++i_; break; }
            throw JsonError("expected ',' or ']' in array");
        }
        return arr;
    }

    std::string parse_string() {
        if (peek() != '"') throw JsonError("expected string");
        ++i_;
        std::string out;
        while (true) {
            if (i_ >= s_.size()) throw JsonError("unterminated string");
            char c = s_[i_++];
            if (c == '"') break;
            if (c == '\\') {
                if (i_ >= s_.size()) throw JsonError("unterminated escape");
                char e = s_[i_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        if (i_ + 4 > s_.size()) throw JsonError("invalid \\u escape");
                        unsigned int cp = std::stoul(s_.substr(i_, 4), nullptr, 16);
                        i_ += 4;
                        // Only the common BMP/ASCII subset is needed for this
                        // project's payloads (addresses, hex, ascii keys) --
                        // encode as UTF-8 without surrogate-pair handling.
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: throw JsonError("invalid escape in string");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    Json parse_number() {
        size_t start = i_;
        bool is_double = false;
        if (s_[i_] == '-') ++i_;
        while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        if (i_ < s_.size() && s_[i_] == '.') {
            is_double = true;
            ++i_;
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            is_double = true;
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        }
        std::string tok = s_.substr(start, i_ - start);
        if (is_double) return Json::number(std::strtod(tok.c_str(), nullptr));
        return Json::integer(std::strtoll(tok.c_str(), nullptr, 10));
    }
};

} // namespace

Json Json::parse(const std::string& text) {
    Parser p(text);
    Json v = p.parse_value();
    p.expect_end();
    return v;
}

} // namespace antchain_node
