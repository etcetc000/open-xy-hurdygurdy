#include "strings.cpp"
#include <cstdio>
#include <cstring>

// Scalar v1.0.0 reference: always recompute each fractional tap. The optimized
// solver must match both its samples and complete delay-line state.
namespace reference {
using namespace hurdygurdy;
const u32 q12 = 4096, neck_mask = neck_size - 1, bridge_mask = bridge_size - 1;
inline i32 clamp(i32 x, i32 lo, i32 hi) { return x < lo ? lo : x > hi ? hi : x; }
inline i32 magnitude(i32 x) { return x < 0 ? -x : x; }
template <u32 Frames>
inline void bow_string(String& s, i32* out, const i32* bow, const u32* periods, u32 position_q10,
                       const Bowing& b) {
    opxy::i16* const neck = s.neck;
    opxy::i16* const bridge = s.bridge;
    u32 at = s.position;
    i32 loss_1 = s.loss_1, loss_2 = s.loss_2;
    u32 neck_now = s.neck_delay, bridge_now = s.bridge_delay;
    const i32 side = b.side, centre = b.centre, slope = b.slope;
    const bool steady = periods[0] == periods[1] && periods[0] == periods[2] && periods[0] == periods[3];
    const u32 span = steady ? Frames : Frames / 4;
    const u32 parts = steady ? 1u : 4u;
    for (u32 part = 0; part < parts; ++part) {
        // Loop delay is neck + bridge + one sample for the symmetric loss filter.
        const u32 total = periods[part] - q12;
        // The bridge side takes whole samples; the neck carries the fraction,
        // so only the neck needs a fractional tap.
        u32 bridge_delay = ((total >> 10) * position_q10) & ~4095u;
        if (bridge_delay < q12) bridge_delay = q12;
        const u32 neck_delay = total - bridge_delay;
        if (!s.primed) {
            neck_now = neck_delay;
            bridge_now = bridge_delay;
            s.primed = 1;
        }
        bridge_now = bridge_delay;
        const u32 bridge_whole = bridge_delay >> 12;
        const i32 change = static_cast<i32>(neck_delay) - static_cast<i32>(neck_now);
        const i32 neck_step = steady ? change / static_cast<i32>(Frames) : change / static_cast<i32>(Frames / 4);
        i32* const block = out + part * span;
        const i32* const push = bow + part * span;
        for (u32 n = 0; n < span; ++n) {
            neck_now = static_cast<u32>(static_cast<i32>(neck_now) + neck_step);
            const i32 arriving = bridge[(at - bridge_whole) & bridge_mask];
            const i32 lossy = (side * (arriving + loss_2) + centre * loss_1) >> 15;
            loss_2 = loss_1;
            loss_1 = arriving;
            const i32 from_bridge = -lossy;
            const i32 from_nut = -(neck_step == 0 ? neck[(at - (neck_now >> 12)) & neck_mask] + (((neck[(at - (neck_now >> 12) - 1u) & neck_mask] - neck[(at - (neck_now >> 12)) & neck_mask]) * static_cast<i32>(neck_now & 4095u)) >> 12) : tap(neck, neck_mask, at, neck_now));
            const i32 slip = clamp(push[n] - (from_bridge + from_nut), -32767, 32767);
            const u32 scaled = static_cast<u32>(magnitude(slip) * slope);
            const u32 index = (scaled + 65536u) >> 17;
            const i32 friction = hg_bow15[index < 1024 ? index : 1024];
            const i32 injected = (slip * friction) >> 15;
            neck[at & neck_mask] = static_cast<opxy::i16>(clamp(from_bridge + injected, -32767, 32767));
            bridge[at & bridge_mask] = static_cast<opxy::i16>(clamp(from_nut + injected, -32767, 32767));
            ++at;
            block[n] = arriving;
        }
        neck_now = neck_delay;
        bridge_now = bridge_delay;
    }
    s.position = at;
    s.loss_1 = loss_1;
    s.loss_2 = loss_2;
    s.neck_delay = neck_now;
    s.bridge_delay = bridge_now;
}
}

template <unsigned Frames> bool exercise() {
    using namespace hurdygurdy;
    const u32 periods_to_test[] = {3*4096, 6*4096+1, 39*4096+3071, 250*4096, 500*4096};
    const u32 contacts[] = {113, 117, 128};
    for (u32 contact : contacts) {
        String actual = {}, expected = {};
        actual.position = 0xfffffff0u;
        u32 random = 9;
        for (auto& sample : actual.neck) {
            random = random * 1664525u + 1013904223u;
            sample = static_cast<opxy::i16>(static_cast<i32>(random >> 16) - 32768);
        }
        for (auto& sample : actual.bridge) {
            random = random * 1664525u + 1013904223u;
            sample = static_cast<opxy::i16>(static_cast<i32>(random >> 16) - 32768);
        }
        expected = actual;
        for (unsigned block = 0; block < 256; ++block) {
            u32 periods[4];
            for (unsigned part = 0; part < 4; ++part)
                periods[part] = periods_to_test[((block / 4) + (block % 4 == 3 ? part : 0)) % 5]
                    + (block % 4 == 2 ? part : 0);
            const i32 side = Frames == 32 ? 700 : 175;
            const Bowing bowing = {0, 8000 + static_cast<i32>(block % 16)*500, side, 31948 - 2*side, 0};
            i32 bow[Frames], got[Frames], want[Frames];
            for (unsigned i = 0; i < Frames; ++i) {
                random = random * 1664525u + 1013904223u;
                bow[i] = static_cast<i32>(random >> 18) - 8192;
            }
            bow_string(actual, got, bow, periods, contact, bowing, Frames);
            reference::bow_string<Frames>(expected, want, bow, periods, contact, bowing);
            if (std::memcmp(got, want, sizeof(got)) || std::memcmp(&actual, &expected, sizeof(actual))) {
                std::fprintf(stderr, "String mismatch: frames=%u contact=%u block=%u\n", Frames, contact, block);
                return false;
            }
        }
    }
    return true;
}
int main() {
    if (!exercise<16>() || !exercise<32>()) return 1;
    std::puts("Fixed and changing delays match the scalar solver, including ring/index wrap");
}
