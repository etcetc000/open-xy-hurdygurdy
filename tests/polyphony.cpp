// The OP-XY plays up to 24 voices at once. Hold 24 notes of this machine,
// spread over three tracks with different controls, through starts, steals,
// parameter automation and releases, and check every output word stays
// inside the declared peak and that sounding work stays bounded by the
// melody lease pool.
#include "engine.cpp"
#include "strings.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using namespace hurdygurdy;
    const unsigned voices = 24, tracks = 3;
    static Shared shared;
    static Voice voice[voices];
    opxy::i32 output[64];
    opxy::u32 pitch[voices][64];
    // Heavy settings per track: drones, buzz with repeating coups, unison.
    const double controls[tracks][8] = {
        {0.62, 0.32, 0.50, 0.15, 0.50, 0.30, 0.65, 0.04},
        {0.80, 0.70, 0.70, 1.00, 0.80, 0.85, 0.90, 0.46},
        {1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 0.96},
    };
    opxy::i32 parameters[tracks][8];
    for (unsigned t = 0; t < tracks; ++t)
        for (unsigned p = 0; p < 8; ++p)
            parameters[t][p] = static_cast<opxy::i32>(controls[t][p] * 2147483647.0);
    for (unsigned v = 0; v < voices; ++v) {
        // Notes from about 55 Hz to 1.8 kHz, spread over five octaves.
        const double hz = 55.0 * __builtin_pow(2.0, (v * 61 % 60) / 12.0);
        for (unsigned i = 0; i < 64; ++i) pitch[v][i] = static_cast<opxy::u32>(hz * 4294967296.0 / 44100.0);
        initialize(voice[v], shared, v + 1);
    }
    unsigned long long nonzero = 0;
    for (unsigned block = 0; block < 4000; ++block) {
        // Restrike a few notes and sweep one control per track as it plays.
        if (block % 250 == 0)
            for (unsigned v = block / 250 % 3; v < voices; v += 5) start(voice[v], shared, v + 1);
        for (unsigned t = 0; t < tracks; ++t)
            parameters[t][0] = static_cast<opxy::i32>(((block * 2654435761u + t * 977u) >> 1) & 0x7fffffffu);
        for (unsigned v = 0; v < voices; ++v) {
            render(voice[v], shared, v + 1, output, parameters[v % tracks], pitch[v]);
            for (unsigned i = 0; i < 64; ++i) {
                require(output[i] >= -644245094 && output[i] <= 644245094, "Output exceeds declared peak");
                nonzero += output[i] != 0;
            }
        }
        unsigned leased = 0;
        for (unsigned v = 0; v < voices; ++v) leased += shared.melody.get(v + 1, voice[v].slot) != nullptr;
        require(leased <= melody_slots, "More sounding melody strings than lease slots");
    }
    require(nonzero > 0, "24 held notes produced silence");
    for (unsigned v = 0; v < voices; ++v) destroy(voice[v], shared, v + 1);
    for (unsigned v = 0; v < voices; ++v)
        require(shared.melody.get(v + 1, voice[v].slot) == nullptr, "Lease survived destroy");
    std::printf("24 held notes on 3 tracks: bounded output, at most %u sounding strings\n",
                static_cast<unsigned>(melody_slots));
}
