#include "engine.cpp"
#include "chien.cpp"
#include "drone_tone.cpp"
#include "strings.cpp"
#include "string_samples.cpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using namespace hurdygurdy;
    static Shared shared;
    Voice voice = {};
    opxy::i32 output[64], parameters[8] = {};
    opxy::u32 pitch[64];
    for (unsigned i = 0; i < 64; ++i) pitch[i] = 42852281u;
    // Every continuous-control corner, every tonic, pitch extremes and modulation.
    // Fresh state makes each corner independent; check every output word.
    for (unsigned setting = 0; setting < 128; ++setting) {
        std::memset(&shared, 0, sizeof(shared));
        voice = Voice{};
        for (unsigned p = 0; p < 7; ++p)
            parameters[p] = (setting & (1u << p)) ? 2147483647 : 0;
        parameters[7] = static_cast<opxy::i32>((2ull * (setting % 12) + 1) * 2147483647ull / 24);
        initialize(voice, shared, 1);
        for (unsigned block = 0; block < 160; ++block) {
            for (unsigned i = 0; i < 64; ++i)
                pitch[i] = block < 80 ? 42852281u : block < 120 ? 0u : 0xffffffffu - i * 1000000u;
            render(voice, shared, 1, output, parameters, pitch);
            for (unsigned i = 0; i < 64; ++i)
                require(output[i] >= -644245094 && output[i] <= 644245094, "Output exceeds declared peak");
        }
        destroy(voice, shared, 1);
    }
    // Switching unison off must eventually stop rendering its delay line.
    std::memset(&shared, 0, sizeof(shared));
    voice = Voice{};
    for (unsigned p = 0; p < 8; ++p) parameters[p] = 0;
    parameters[0] = parameters[4] = 1073741824;
    parameters[3] = 2147483647;
    for (unsigned i = 0; i < 64; ++i) pitch[i] = 42852281u;
    initialize(voice, shared, 1);
    for (unsigned b = 0; b < 100; ++b) render(voice, shared, 1, output, parameters, pitch);
    parameters[3] = 0;
    for (unsigned b = 0; b < 1000; ++b) render(voice, shared, 1, output, parameters, pitch);
    require(voice.twin_gain == 0, "Unison fade stalls above zero and keeps rendering the twin");
    MelodySlot* slot = shared.melody.get(1, voice.slot);
    require(slot != nullptr, "Missing melody lease");
    const unsigned position = slot->twin.position;
    render(voice, shared, 1, output, parameters, pitch);
    require(position == slot->twin.position, "Disabled unison still renders");
    // A ninth owner takes the oldest string lease; a stolen owner renders silence.
    Voice others[8] = {};
    for (unsigned i = 0; i < 8; ++i) initialize(others[i], shared, i + 2);
    require(shared.melody.get(1, voice.slot) == nullptr, "Oldest melody was not stolen");
    render(voice, shared, 1, output, parameters, pitch);
    for (unsigned i = 0; i < 64; ++i) require(output[i] == 0, "Stolen voice is not silent");
    destroy(voice, shared, 1);
    require(shared.melody.get(9, others[7].slot) != nullptr, "Stale destroy released another owner's lease");
    std::puts("Audio: control corners, pitch extremes, peak bounds, unison shutdown and lease stealing passed");
}
