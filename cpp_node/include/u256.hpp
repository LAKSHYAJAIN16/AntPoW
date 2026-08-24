#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace antchain_node {

// Fixed-width unsigned 256-bit integer -- real (not probabilistic) target
// arithmetic for consensus, mirroring node/antchain_node/consensus.py's use
// of Python's arbitrary-precision ints. limb[0] is least significant,
// limb[3] most significant.
struct U256 {
    std::array<uint64_t, 4> limb{{0, 0, 0, 0}};

    static U256 zero() { return U256{}; }
    static U256 max();                       // 2^256 - 1
    static U256 from_u64(uint64_t v);
    static U256 from_hex(const std::string& hex64); // exactly 64 hex chars, big-endian
    std::string to_hex() const;                     // 64 lowercase hex chars, big-endian

    bool is_zero() const;
    bool get_bit(int bit) const;   // bit in [0,255], 0 = least significant
    void set_bit(int bit, bool v);

    U256 shl(unsigned bits) const;
    U256 shr(unsigned bits) const;
    U256 sub(const U256& other) const; // requires *this >= other
    // Wraps mod 2^256 on overflow (no clamping) -- only used to sum
    // per-block work in Blockchain::cumulative_work, which never gets
    // close to 2^256 for a toy/educational chain's realistic block counts.
    U256 add(const U256& other) const;

    bool operator<(const U256& o) const;
    bool operator<=(const U256& o) const { return *this < o || *this == o; }
    bool operator==(const U256& o) const { return limb == o.limb; }

    // Lossy int->double conversion, deliberately mirroring the precision
    // loss Python incurs when it computes `t0 * multiplier` (a Python int
    // times a float implicitly downcasts the int through a double first) --
    // see consensus.cpp::quality_target/retarget.
    double to_double() const;
    static U256 from_double_clamped(double d); // clamps to [0, max()]

    static U256 divide(const U256& a, const U256& b); // a // b, b != 0
};

} // namespace antchain_node
