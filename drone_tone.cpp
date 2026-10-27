#include "drone_tone.h"
#include "drone_waves.h"

namespace hurdygurdy {
void drone_tone(DroneTone& tone, i32* out, const i32* bow, u32 tonic, u32 role,
                i32 rosin, i32 wheel) {
    const u32 key = tonic * 3 + role;
    const DroneWavePoint* const wave_pair = drone_waves[key];
    const u32 step = drone_steps[key];
    const i32 target = rosin >> 16;
    const i32 cutoff = 20000 + (wheel >> 18);
    u32 phase = tone.phase;
    i32 colour = tone.colour, filtered = tone.filtered;
    for (u32 i = 0; i < render_frames; ++i) {
        colour += (target - colour) >> 6;
        const DroneWavePoint* const point = wave_pair + (phase >> 25);
        const i32 fraction = static_cast<i32>((phase >> 10) & 32767u);
        const i32 a = point[0].low + (((point[1].low - point[0].low) * fraction) >> 15);
        const i32 b = point[0].high + (((point[1].high - point[0].high) * fraction) >> 15);
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
