#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace antchain_node {

inline std::string to_hex(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out[2 * i] = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 0xF];
    }
    return out;
}

template <size_t N>
inline std::string to_hex(const std::array<uint8_t, N>& data) {
    return to_hex(data.data(), N);
}

inline std::string to_hex(const std::vector<uint8_t>& data) {
    return to_hex(data.data(), data.size());
}

inline int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("invalid hex digit");
}

inline std::vector<uint8_t> from_hex(const std::string& s) {
    if (s.size() % 2 != 0) throw std::invalid_argument("odd-length hex string");
    std::vector<uint8_t> out(s.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<uint8_t>((hex_nibble(s[2 * i]) << 4) | hex_nibble(s[2 * i + 1]));
    }
    return out;
}

} // namespace antchain_node
