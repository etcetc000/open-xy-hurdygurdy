#include "bowed_string.h"
#include "tables.h"

namespace hurdygurdy {
namespace string_solver {
const u32 q12 = 4096;
const u32 neck_mask = neck_size - 1;
const u32 bridge_mask = bridge_size - 1;
inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 magnitude(i32 v) { return v < 0 ? -v : v; }

// A settled delay keeps its tap fraction and whole delay constant for the
// entire span. Pitch bends retain the original per-sample delay ramp.
template <bool Fixed>
inline void samples(String& s, i32* out, const i32* bow, u32 count,
                    u32 bridge_whole, i32 neck_step, const Bowing& b) {
    opxy::i16* const neck = s.neck;
    opxy::i16* const bridge = s.bridge;
    u32 at = s.position, neck_now = s.neck_delay;
    i32 loss_1 = s.loss_1, loss_2 = s.loss_2;
    const i32 side = b.side, centre = b.centre, slope = b.slope;
    const u32 whole = neck_now >> 12;
    const i32 fraction = static_cast<i32>(neck_now & 4095u);
    for (u32 n = 0; n < count; ++n) {
        if (!Fixed) neck_now = static_cast<u32>(static_cast<i32>(neck_now) + neck_step);
        const i32 arriving = bridge[(at - bridge_whole) & bridge_mask];
        const i32 lossy = (side * (arriving + loss_2) + centre * loss_1) / 32768;
        loss_2 = loss_1;
        loss_1 = arriving;
        const i32 from_bridge = -lossy;
        const i32 a = Fixed ? neck[(at - whole) & neck_mask] : 0;
        const i32 previous = Fixed ? neck[(at - whole - 1u) & neck_mask] : 0;
        const i32 from_nut = -(Fixed ? a + (previous - a) * fraction / 4096
                                    : tap(neck, neck_mask, at, neck_now));
        const i32 slip = clamp(bow[n] - clamp(from_bridge + from_nut, -32767, 32767), -32767, 32767);
        const u32 scaled = static_cast<u32>(magnitude(slip) * slope);
        const u32 index = (scaled + 65536u) >> 17;
        const i32 friction = hg_bow15[index < 1024 ? index : 1024];
        const i32 injected = slip * friction / 32768;
        neck[at & neck_mask] = static_cast<opxy::i16>(clamp(from_bridge + injected, -32767, 32767));
        bridge[at & bridge_mask] = static_cast<opxy::i16>(clamp(from_nut + injected, -32767, 32767));
        ++at;
        out[n] = arriving;
    }
    s.position = at;
    s.loss_1 = loss_1;
    s.loss_2 = loss_2;
}
} // namespace string_solver

// One shared native implementation serves the melody, twin and three drones.
// Only 16- and 32-frame calls are used; all delay divisions stay constant.
void bow_string(String& s, i32* out, const i32* bow, const u32* periods,
                u32 position_q10, const Bowing& b, u32 frames) {
    const bool steady = periods[0] == periods[1] && periods[0] == periods[2] && periods[0] == periods[3];
    const u32 span = steady ? frames : frames / 4;
    const u32 parts = steady ? 1u : 4u;
    for (u32 part = 0; part < parts; ++part) {
        const u32 total = periods[part] - string_solver::q12;
        u32 bridge_delay = ((total >> 10) * position_q10) & ~4095u;
        if (bridge_delay < string_solver::q12) bridge_delay = string_solver::q12;
        const u32 neck_delay = total - bridge_delay;
        if (!s.primed) {
            s.neck_delay = neck_delay;
            s.primed = 1;
        }
        const i32 change = static_cast<i32>(neck_delay) - static_cast<i32>(s.neck_delay);
        const i32 step = steady ? (frames == 32 ? change / 32 : change / 16)
                                : (frames == 32 ? change / 8 : change / 4);
        if (step == 0)
            string_solver::samples<true>(s, out + part * span, bow + part * span, span, bridge_delay >> 12, step, b);
        else
            string_solver::samples<false>(s, out + part * span, bow + part * span, span, bridge_delay >> 12, step, b);
        s.neck_delay = neck_delay;
        s.bridge_delay = bridge_delay;
    }
}
} // namespace hurdygurdy
