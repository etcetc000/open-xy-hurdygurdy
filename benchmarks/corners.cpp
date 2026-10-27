// Portable wall-time benchmark. See AUDIT.md for the separate host-cycle probe.
#include "engine.cpp"
#include "drone_tone.cpp"
#include "strings.cpp"
#include "string_samples.cpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

struct Case {
    unsigned controls, note, modulated, tonic;
    double cost;
};
static volatile opxy::u32 sink = 0;
static const unsigned pitches[] = {
    10713070u, 21426140u, 42852281u, 85704562u,
    101918891u, 171409124u, 0u, 0xffffffffu
};

static double measure(const Case& setting) {
    static hurdygurdy::Shared shared;
    hurdygurdy::Voice voice;
    opxy::i32 parameters[8], output[64];
    opxy::u32 pitch[64];
    double best = 1e30;
    for (unsigned repeat = 0; repeat < 7; ++repeat) {
        std::memset(&shared, 0, sizeof(shared));
        std::memset(&voice, 0, sizeof(voice));
        for (unsigned k = 0; k < 7; ++k)
            parameters[k] = (setting.controls & (1u << k)) ? 2147483647 : 0;
        parameters[7] = static_cast<opxy::i32>((2ull * setting.tonic + 1) * 2147483647ull / 24);
        for (unsigned i = 0; i < 64; ++i)
            pitch[i] = pitches[setting.note] + (setting.modulated ? (i / 16) * 300000u : 0u);
        hurdygurdy::initialize(voice, shared, 1);
        for (unsigned block = 0; block < 256; ++block)
            hurdygurdy::render(voice, shared, 1, output, parameters, pitch);
        const auto start = std::chrono::steady_clock::now();
        for (unsigned block = 0; block < 2048; ++block) {
            if (setting.modulated) for (unsigned part = 0; part < 4; ++part)
                pitch[part * 16] = pitches[setting.note] + ((block * 977u + part * 230003u) & 0xfffffu);
            hurdygurdy::render(voice, shared, 1, output, parameters, pitch);
            sink += static_cast<opxy::u32>(output[block & 63]);
        }
        const double elapsed = std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - start).count() / 2048;
        if (elapsed < best) best = elapsed;
    }
    return best;
}

int main() {
    Case cases[2816];
    unsigned count = 0;
    for (unsigned modulated = 0; modulated < 2; ++modulated)
        for (unsigned controls = 0; controls < 128; ++controls)
            for (unsigned note = 0; note < 8; ++note)
                cases[count++] = Case{controls, note, modulated, controls % 12, 0};
    // Exercise every tonic in four heavy control combinations as well.
    const unsigned heavy[] = {127, 111, 95, 75};
    for (unsigned controls : heavy) for (unsigned tonic = 0; tonic < 12; ++tonic)
        for (unsigned note = 0; note < 8; ++note)
            for (unsigned modulated = 0; modulated < 2; ++modulated)
                cases[count++] = Case{controls, note, modulated, tonic, 0};
    for (auto& setting : cases) setting.cost = measure(setting);
    std::sort(cases, cases + count, [](const Case& a, const Case& b) { return a.cost > b.cost; });
    std::puts("controls,pitch_index,modulated,tonic,ns_per_block");
    for (const auto& setting : cases)
        std::printf("%u,%u,%u,%u,%.1f\n", setting.controls, setting.note,
                    setting.modulated, setting.tonic, setting.cost);
}
