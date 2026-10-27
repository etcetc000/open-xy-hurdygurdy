#include "engine.cpp"
#include "drone_tone.cpp"
#include "strings.cpp"
#include "string_samples.cpp"
#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using namespace hurdygurdy;
    struct Guarded {
        u32 before;
        DetunedCopy state;
        u32 after;
    } guarded = {};
    guarded.before = 0x12345678u;
    guarded.after = 0xabcdef09u;
    guarded.state.position = 0xfffffff0u;
    i32 input[16], output[16];
    const u32 delays[] = {3 * 4096, 250 * 4096, 39 * 4096 + 3071, 6 * 4096 + 1};
    u32 random = 17;
    // Sweep within a block and jump across the full supported pitch range.
    // Include signed sample extremes and the unsigned history-index wrap.
    for (u32 block = 0; block < 2000; ++block) {
        u32 periods[4];
        for (u32 part = 0; part < 4; ++part) periods[part] = delays[(block + part) & 3u];
        for (u32 i = 0; i < 16; ++i) {
            random = random * 1664525u + 1013904223u;
            input[i] = block & 1 ? (i & 1 ? 32767 : -32767)
                                 : static_cast<i32>(random >> 17) - 16384;
        }
        const i32 ratio = lookup33(hg_twin_ratio, block & 1 ? 2147483647 : 0);
        detuned_copy(guarded.state, output, input, ratio, periods);
        for (i32 sample : output) require(sample >= -32767 && sample <= 32767, "Copy amplified a bounded input");
        require(guarded.state.phase < (periods[3] << 5), "Read phase escaped the current window");
        require(guarded.before == 0x12345678u && guarded.after == 0xabcdef09u, "History escaped fixed storage");
    }
    // A feed-forward copy must drain fully: no feedback, trapped DC or stale echo.
    for (auto& sample : input) sample = 0;
    const u32 periods[4] = {250 * 4096, 250 * 4096, 250 * 4096, 250 * 4096};
    for (u32 block = 0; block < 64; ++block)
        detuned_copy(guarded.state, output, input, hg_twin_ratio[32], periods);
    for (i32 sample : output) require(sample == 0, "Copy did not drain after silence");
    // Constant input stays constant across phase wraps and parameter changes.
    for (auto& sample : guarded.state.history) sample = 12345;
    for (auto& sample : input) sample = 12345;
    for (u32 block = 0; block < 512; ++block) {
        u32 changing[4];
        for (u32 part = 0; part < 4; ++part) changing[part] = delays[(block + part) & 3u];
        detuned_copy(guarded.state, output, input, hg_twin_ratio[block % 33], changing);
        for (i32 sample : output) require(sample == 12345, "Crossfade changed a constant input");
    }
    std::puts("Detuned copy: bounded automation, index wrap, constant gain and silence drain passed");
}
