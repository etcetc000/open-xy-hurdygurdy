// Compile with -O2 and the SDK and machine include directories. Host timing only.
#include "engine.cpp"
#include "control_division.cpp"
#include "chien.cpp"
#include "drone_tone.cpp"
#include "strings.cpp"
#include "string_samples.cpp"
#include <chrono>
#include <cstdio>
#include <cstring>

int main() {
    static hurdygurdy::Shared shared;
    static hurdygurdy::Voice voices[8];
    opxy::i32 parameters[8] = {}, output[64];
    opxy::u32 pitch[64];
    volatile opxy::u32 sink = 0;
    for (unsigned i = 0; i < 64; ++i) pitch[i] = 42852281u;
    const char* names[] = {"melody", "unison", "drones", "all", "unison-off-after-on"};
    for (unsigned count = 1; count <= 8; count *= 8) {
        for (unsigned mode = 0; mode < 5; ++mode) {
            double best = 1e30;
            for (unsigned repeat = 0; repeat < 5; ++repeat) {
                std::memset(&shared, 0, sizeof(shared));
                std::memset(voices, 0, sizeof(voices));
                for (unsigned p = 0; p < 8; ++p) parameters[p] = 0;
                parameters[0] = parameters[2] = parameters[4] = 1073741824;
                if (mode == 1 || mode == 3 || mode == 4) parameters[3] = 2147483647;
                if (mode == 2 || mode == 3) parameters[1] = parameters[5] = parameters[6] = 2147483647;
                for (unsigned v = 0; v < count; ++v) hurdygurdy::initialize(voices[v], shared, v + 1);
                for (unsigned b = 0; b < 1200; ++b) {
                    if (mode == 4 && b == 100) parameters[3] = 0;
                    for (unsigned v = 0; v < count; ++v)
                        hurdygurdy::render(voices[v], shared, v + 1, output, parameters, pitch);
                }
                const auto begin = std::chrono::steady_clock::now();
                for (unsigned b = 0; b < 2000; ++b)
                    for (unsigned v = 0; v < count; ++v) {
                        hurdygurdy::render(voices[v], shared, v + 1, output, parameters, pitch);
                        sink = sink + static_cast<opxy::u32>(output[b & 63]);
                    }
                const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / 2000;
                if (ns < best) best = ns;
            }
            std::printf("%u voices %-22s %.1f ns / collection block\n", count, names[mode], best);
        }
    }
    // Diagnostic: note-start order differs from callback traversal order.
    std::memset(&shared, 0, sizeof(shared));
    std::memset(voices, 0, sizeof(voices));
    for (unsigned p = 0; p < 8; ++p) parameters[p] = 1073741824;
    hurdygurdy::initialize(voices[1], shared, 2);
    hurdygurdy::initialize(voices[0], shared, 1); // actual newest note
    hurdygurdy::render(voices[0], shared, 1, output, parameters, pitch);
    hurdygurdy::render(voices[1], shared, 2, output, parameters, pitch);
    std::printf("Ownership diagnostic: newest=1, actual=%u, drone renders=%u\n", shared.drone_owner, shared.drone_serial);
}
