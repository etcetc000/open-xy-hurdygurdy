#include "control_division.h"
#include "reciprocal_seeds.h"
#include <opxy/q31.hpp>

namespace hurdygurdy {
namespace {
// Normalize to [2^29, 2^30), then refine a lower reciprocal seed twice.
// The final integer quotient is at most one away over the declared domains;
// one remainder comparison corrects it exactly. The exhaustive host test
// checks every callback divisor against ordinary unsigned integer division.
inline u32 estimate(u32 denominator, u32 exponent) {
    u32 normalized = denominator, shift = 0;
    if (normalized < (1u << 14)) { normalized <<= 16; shift += 16; }
    if (normalized < (1u << 22)) { normalized <<= 8; shift += 8; }
    if (normalized < (1u << 26)) { normalized <<= 4; shift += 4; }
    if (normalized < (1u << 28)) { normalized <<= 2; shift += 2; }
    if (normalized < (1u << 29)) { normalized <<= 1; shift += 1; }
    i32 reciprocal = reciprocal_seeds[(normalized >> 21) - 256u];
    const i32 value = static_cast<i32>(normalized);
    i32 error = 268435456 - opxy::q31::multiply(value, reciprocal);
    reciprocal += opxy::q31::multiply(reciprocal, error) * 8;
    error = 268435456 - opxy::q31::multiply(value, reciprocal);
    reciprocal += opxy::q31::multiply(reciprocal, error) * 8;
    return static_cast<u32>(reciprocal) >> (59u - exponent - shift);
}
}

u32 divide_phase_period(u32 denominator) {
    u32 quotient = estimate(denominator, 32);
    const u32 product = quotient * denominator;
    // A one-too-large quotient wraps its product through 2^32.
    if (product < denominator) --quotient;
    else if (0xffffffffu - product >= denominator) ++quotient;
    return quotient;
}

u32 divide_window_weight(u32 denominator) {
    u32 quotient = estimate(denominator, 31);
    const u32 product = quotient * denominator;
    if (product > 0x80000000u) --quotient;
    else if (0x80000000u - product >= denominator) ++quotient;
    return quotient;
}

u32 detune_step(u32 denominator) {
    return detune_steps[65535u - denominator];
}
}
