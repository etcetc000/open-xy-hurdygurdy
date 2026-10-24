#pragma once
#include "core.h"

namespace hurdygurdy {
// Wheel/string interaction coefficients; audio coefficients are Q15.
struct Bowing {
    i32 speed; // target bow velocity, Q31
    i32 slope, side, centre, roughness;
};

// Fractional delay interpolation at the delay lines' Q15 precision.
inline i32 tap(const opxy::i16* line, u32 mask, u32 position, u32 delay) {
    const u32 whole = delay >> 12;
    const i32 fraction = static_cast<i32>(delay & 4095u);
    const i32 a = line[(position - whole) & mask];
    const i32 b = line[(position - whole - 1u) & mask];
    return a + (b - a) * fraction / 4096;
}

void bow_string(String&, i32* out, const i32* bow, const u32* periods,
                u32 position_q10, const Bowing&, u32 frames);
} // namespace hurdygurdy
