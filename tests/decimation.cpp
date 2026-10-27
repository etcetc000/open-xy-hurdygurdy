#include "engine.cpp"
#include "drone_tone.cpp"
#include "strings.cpp"
#include "string_samples.cpp"
#include <cstdio>

int main() {
    hurdygurdy::String state = {};
    opxy::i32 history[7] = {}, input[32], actual[16], expected[16];
    const opxy::i32 coefficients[7] = {-1475, 0, 9667, 16384, 9667, 0, -1475};
    opxy::u32 random = 1;
    for (unsigned block = 0; block < 4096; ++block) {
        for (unsigned i = 0; i < 32; ++i) {
            random = random * 1664525u + 1013904223u;
            input[i] = block == 0 ? (i == 0 ? 32767 : 0)
                : block < 8 ? ((i & 1) ? -32767 : 32767)
                : static_cast<opxy::i32>(random >> 16) - 32768;
            for (unsigned tap = 6; tap; --tap) history[tap] = history[tap - 1];
            history[0] = input[i];
            if (i & 1) {
                opxy::i64 sum = 0;
                for (unsigned tap = 0; tap < 7; ++tap)
                    sum += static_cast<opxy::i64>(coefficients[tap]) * history[tap];
                const opxy::i64 value = sum / 32768;
                expected[i / 2] = static_cast<opxy::i32>(value < -32767 ? -32767 : value > 32767 ? 32767 : value);
            }
        }
        hurdygurdy::decimate(state, actual, input);
        for (unsigned i = 0; i < 16; ++i) if (actual[i] != expected[i]) {
            std::fprintf(stderr, "Half-band mismatch at block %u, sample %u\n", block, i);
            return 1;
        }
    }
    std::puts("Polyphase decimator matches the full seven-tap FIR across block boundaries");
}
