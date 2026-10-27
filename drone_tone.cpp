#include "drone_tone.h"
#include "drone_waves.h"

namespace hurdygurdy {
namespace {
inline i32 wave_at(const i32* wave, u32 phase) {
    const u32 at = phase >> 25;
    const i32 fraction = static_cast<i32>((phase >> 10) & 32767u);
    const i32 a = wave[at], b = wave[at + 1];
    return a + (((b - a) * fraction) >> 15);
}
}

void drone_tone(DroneTone& tone, i32* out, const i32* bow, u32 tonic, u32 role,
                i32 rosin, i32 wheel) {
    const u32 key = tonic * 3 + role;
    const i32* const low = drone_waves[key * 2];
    const i32* const high = drone_waves[key * 2 + 1];
    const u32 step = drone_steps[key];
    const i32 target = rosin >> 16;
    const i32 cutoff = 20000 + (wheel >> 18);
    u32 phase = tone.phase;
    i32 colour = tone.colour, filtered = tone.filtered;
    for (u32 i = 0; i < render_frames; ++i) {
        colour += (target - colour) >> 6;
        const i32 a = wave_at(low, phase), b = wave_at(high, phase);
        const i32 wave = a + (((b - a) * colour) >> 15);
        const i32 pushed = (wave * bow[i]) >> 10;
        const i32 limited = pushed < -32767 ? -32767 : pushed > 32767 ? 32767 : pushed;
        filtered += ((limited - filtered) * cutoff) >> 15;
        out[i] = filtered;
        phase += step;
    }
    tone.phase = phase;
    tone.position += render_frames;
    tone.colour = colour;
    tone.filtered = filtered;
}
} // namespace hurdygurdy
