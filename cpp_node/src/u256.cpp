#include "u256.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace antchain_node {

U256 U256::max() {
    U256 v;
    v.limb = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    return v;
}

U256 U256::from_u64(uint64_t v) {
    U256 out;
    out.limb[0] = v;
    return out;
}

U256 U256::from_hex(const std::string& hex64) {
    if (hex64.size() != 64) throw std::invalid_argument("U256::from_hex requires exactly 64 hex chars");
    U256 out;
    // limb[3] = chars[0:16] (most significant) ... limb[0] = chars[48:64]
    for (int limb_idx = 3; limb_idx >= 0; --limb_idx) {
        size_t offset = (3 - static_cast<size_t>(limb_idx)) * 16;
        uint64_t v = 0;
        for (size_t k = 0; k < 16; ++k) {
            char c = hex64[offset + k];
            int nibble;
            if (c >= '0' && c <= '9') nibble = c - '0';
            else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
            else throw std::invalid_argument("U256::from_hex: invalid hex digit");
            v = (v << 4) | static_cast<uint64_t>(nibble);
        }
        out.limb[static_cast<size_t>(limb_idx)] = v;
    }
    return out;
}

std::string U256::to_hex() const {
    char buf[65];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx%016llx%016llx",
                  static_cast<unsigned long long>(limb[3]), static_cast<unsigned long long>(limb[2]),
                  static_cast<unsigned long long>(limb[1]), static_cast<unsigned long long>(limb[0]));
    return std::string(buf, 64);
}

bool U256::is_zero() const {
    return limb[0] == 0 && limb[1] == 0 && limb[2] == 0 && limb[3] == 0;
}

bool U256::get_bit(int bit) const {
    return (limb[static_cast<size_t>(bit) / 64] >> (static_cast<unsigned>(bit) % 64)) & 1ULL;
}

void U256::set_bit(int bit, bool v) {
    uint64_t mask = 1ULL << (static_cast<unsigned>(bit) % 64);
    size_t idx = static_cast<size_t>(bit) / 64;
    if (v) limb[idx] |= mask; else limb[idx] &= ~mask;
}

U256 U256::shl(unsigned bits) const {
    if (bits == 0) return *this;
    U256 out;
    if (bits >= 256) return out; // all zero
    unsigned limb_shift = bits / 64;
    unsigned bit_shift = bits % 64;
    for (int i = 3; i >= 0; --i) {
        unsigned src = static_cast<unsigned>(i) - limb_shift;
        if (static_cast<unsigned>(i) < limb_shift) continue;
        uint64_t v = limb[src];
        uint64_t hi = (bit_shift == 0) ? v : (v << bit_shift);
        out.limb[static_cast<size_t>(i)] |= hi;
        if (bit_shift != 0 && src >= 1) {
            uint64_t lo = limb[src - 1] >> (64 - bit_shift);
            out.limb[static_cast<size_t>(i)] |= lo;
        }
    }
    return out;
}

U256 U256::shr(unsigned bits) const {
    if (bits == 0) return *this;
    U256 out;
    if (bits >= 256) return out; // all zero
    unsigned limb_shift = bits / 64;
    unsigned bit_shift = bits % 64;
    for (int i = 0; i < 4; ++i) {
        unsigned src = static_cast<unsigned>(i) + limb_shift;
        if (src > 3) continue;
        uint64_t v = limb[src];
        uint64_t lo = (bit_shift == 0) ? v : (v >> bit_shift);
        out.limb[static_cast<size_t>(i)] |= lo;
        if (bit_shift != 0 && src + 1 <= 3) {
            uint64_t hi = limb[src + 1] << (64 - bit_shift);
            out.limb[static_cast<size_t>(i)] |= hi;
        }
    }
    return out;
}

U256 U256::sub(const U256& other) const {
    // Plain uint64 subtract-with-borrow (no __int128 -- MSVC doesn't have
    // it). Unsigned wraparound on `diff` is well-defined and gives the
    // correct low-64-bits-of-the-true-difference in every case.
    U256 out;
    uint64_t borrow = 0;
    for (int i = 0; i < 4; ++i) {
        uint64_t a = limb[static_cast<size_t>(i)];
        uint64_t b = other.limb[static_cast<size_t>(i)];
        uint64_t diff = a - b - borrow;
        borrow = (a < b) || (a == b && borrow != 0) ? 1 : 0;
        out.limb[static_cast<size_t>(i)] = diff;
    }
    return out;
}

U256 U256::add(const U256& other) const {
    U256 out;
    uint64_t carry = 0;
    for (int i = 0; i < 4; ++i) {
        uint64_t a = limb[static_cast<size_t>(i)];
        uint64_t b = other.limb[static_cast<size_t>(i)];
        uint64_t sum = a + b + carry;
        carry = (sum < a || (carry != 0 && sum == a)) ? 1 : 0;
        out.limb[static_cast<size_t>(i)] = sum;
    }
    return out;
}

bool U256::operator<(const U256& o) const {
    for (int i = 3; i >= 0; --i) {
        if (limb[static_cast<size_t>(i)] != o.limb[static_cast<size_t>(i)])
            return limb[static_cast<size_t>(i)] < o.limb[static_cast<size_t>(i)];
    }
    return false;
}

double U256::to_double() const {
    double d = 0.0;
    for (int i = 3; i >= 0; --i) {
        d = d * 18446744073709551616.0 /* 2^64 */ + static_cast<double>(limb[static_cast<size_t>(i)]);
    }
    return d;
}

U256 U256::from_double_clamped(double d) {
    if (!(d > 0.0)) return zero();
    static const double MAX_D = std::ldexp(1.0, 256);
    if (d >= MAX_D) return max();
    int exp = 0;
    double mant = std::frexp(d, &exp); // d = mant * 2^exp, 0.5 <= mant < 1
    uint64_t mant64 = static_cast<uint64_t>(std::ldexp(mant, 53)); // 2^52 <= mant64 < 2^53
    int shift = exp - 53; // d ~= mant64 * 2^shift
    U256 result = from_u64(mant64);
    if (shift >= 0) {
        result = result.shl(static_cast<unsigned>(shift));
    } else {
        result = result.shr(static_cast<unsigned>(-shift));
    }
    return result;
}

U256 U256::divide(const U256& a, const U256& b) {
    if (b.is_zero()) throw std::domain_error("U256 division by zero");
    U256 quotient;
    U256 remainder;
    for (int bit = 255; bit >= 0; --bit) {
        remainder = remainder.shl(1);
        if (a.get_bit(bit)) remainder.limb[0] |= 1ULL;
        if (!(remainder < b)) {
            remainder = remainder.sub(b);
            quotient.set_bit(bit, true);
        }
    }
    return quotient;
}

} // namespace antchain_node
