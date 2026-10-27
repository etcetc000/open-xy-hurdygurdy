#include "chien.h"
#include <opxy/q31.hpp>

namespace hurdygurdy {
namespace chien_detail {
const i32 maximum = 2147483647;
inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 magnitude(i32 v) { return v < 0 ? -v : v; }
// Both callers add nonnegative lift/envelope values. The unsigned sum fits
// u32, so saturation needs only the upper bound, with no signed overflow.
inline i32 add_positive(i32 a, i32 b) {
    const u32 sum = static_cast<u32>(a) + static_cast<u32>(b);
    return sum > static_cast<u32>(maximum) ? maximum : static_cast<i32>(sum);
}
inline i32 roughen(u32& random, i32& noise) {
    random = random * 1664525u + 1013904223u;
    const i32 white = static_cast<i32>(random);
    noise += (white >> 3) - (noise >> 3);
    return noise >> 17;
}
const i32 coup_decay = 2144489075;       // 65 ms wrist-stroke envelope
const i32 rattle_b0 = 183390350;          // chien resonator, 2.2 kHz, Q 3 (halved)
const i32 rattle_a1 = 543188326;
const i32 rattle_a2 = 706961124;
} // namespace chien_detail
void render_chien(DroneSet& d, i32* mixed, const i32* work,
                  i32 level_target, i32 buzz_target, u32 rate) {
    i32 level;
    // Chien: the trompette lifts its buzzing bridge when the wheel is stroked
    // (a coup) and, above half Buzz, all the time. Impacts come from the peak
    // of each swing, so the buzz is pitched with the trompette.
    i32 buzz = d.buzz;
    const bool chien = buzz_target > 0 || buzz > 0;
    // Steady lift above Buzz 0.45, full at 1; coup sensitivity full by 0.4.
    const i32 steady = buzz_target <= 966367642 ? 0
        : chien_detail::add_positive(buzz_target - 966367642, buzz_target - 966367642);
    const i32 sensitivity = buzz_target >= 858993459 ? chien_detail::maximum : buzz_target + buzz_target + buzz_target / 2;
    level = d.level;
    for (u32 i = 0; i < render_frames; ++i) {
        level += (level_target - level) >> 6;
        buzz += (buzz_target - buzz) >> 6;
        if (rate) {
            const u32 before = d.coup_phase;
            d.coup_phase += rate * rate_factor;
            if (d.coup_phase < before && d.coup < 1717986918) d.coup = 1717986918;  // 0.8
        }
        d.coup = opxy::q31::multiply(d.coup, chien_detail::coup_decay);
        const i32 t = work[i];
        i32 out = (t * (level >> 17)) >> 15;                          // 0.50
        if (chien) {
            const i32 lift_target = chien_detail::add_positive(steady, opxy::q31::multiply(d.coup, sensitivity));
            d.lift += (lift_target - d.lift) >> 3;
            d.envelope += (chien_detail::magnitude(t) - d.envelope) >> 4;
            i32 peak = t - (d.envelope >> 1);
            if (peak < 0) peak = 0;
            const i32 impact = peak - d.rectified;
            d.rectified = peak;
            const i32 rough = chien_detail::roughen(d.random, d.noise);
            const i32 jagged = chien_detail::clamp(impact + ((impact * (rough * 2)) >> 15), -32767, 32767);
            const i32 rattle = chien_detail::clamp(((chien_detail::rattle_b0 / 131072) * jagged + (chien_detail::rattle_a1 / 131072) * d.rattle_1
                                - (chien_detail::rattle_a2 / 131072) * d.rattle_2) >> 13, -32767, 32767);
            d.rattle_2 = d.rattle_1;
            d.rattle_1 = rattle;
            // The rasp is loud on a real gurdy: lift the resonator 30 dB.
            const i32 loudness = 1073741824 + (buzz >> 1);                  // 0.5 .. 1 with Buzz
            const i32 chatter = chien_detail::clamp((((rattle * (d.lift >> 16)) >> 15) * (loudness >> 16)) >> 15, -204, 204);
            out += chatter * 16;                                   // peak 0.1
        }
        mixed[i] = chien_detail::clamp(mixed[i] + out, -32767, 32767);
    }
    d.level = level;
    d.buzz = buzz;
}
} // namespace hurdygurdy
