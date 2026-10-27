#include "bowed_string.h"

namespace hurdygurdy {
// Separate translation unit keeps delay setup out of the sample kernels.
void settled_samples(String&, i32*, const i32*, u32, u32, i32, const Bowing&);
void moving_samples(String&, i32*, const i32*, u32, u32, i32, const Bowing&);

// One shared native implementation serves the melody and three drones.
// Only 16- and 32-frame calls are used; all delay divisions stay constant.
void bow_string(String& s, i32* out, const i32* bow, const u32* periods,
                u32 position_q10, const Bowing& b, u32 frames) {
    const bool steady = periods[0] == periods[1] && periods[0] == periods[2] && periods[0] == periods[3];
    const u32 span = steady ? frames : frames / 4;
    const u32 parts = steady ? 1u : 4u;
    for (u32 part = 0; part < parts; ++part) {
        const u32 total = periods[part] - 4096u;
        u32 bridge_delay = ((total >> 10) * position_q10) & ~4095u;
        if (bridge_delay < 4096u) bridge_delay = 4096u;
        const u32 neck_delay = total - bridge_delay;
        if (!s.primed) {
            s.neck_delay = neck_delay;
            s.primed = 1;
        }
        const i32 change = static_cast<i32>(neck_delay) - static_cast<i32>(s.neck_delay);
        const i32 step = steady ? (frames == 32 ? change / 32 : change / 16)
                                : (frames == 32 ? change / 8 : change / 4);
        if (step == 0)
            settled_samples(s, out + part * span, bow + part * span, span, bridge_delay >> 12, step, b);
        else
            moving_samples(s, out + part * span, bow + part * span, span, bridge_delay >> 12, step, b);
        s.neck_delay = neck_delay;
        s.bridge_delay = bridge_delay;
    }
}
} // namespace hurdygurdy
