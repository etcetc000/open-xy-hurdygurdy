#include "engine.cpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
static const opxy::i32 presets[][8] = {
    {1181116006, 0, 1073741824, 0, 966367641, 0, 0, 89479201}, // Chanterelle
    {1331439861, 687194767, 1073741824, 322122547, 1073741824, 644245094, 1395864371, 89479201}, // Bourree
    {966367641, 0, 1331439861, 429496729, 751619276, 0, 966367641, 447391711}, // Vielle
    {751619276, 0, 2040109465, 1181116006, 644245094, 0, 1717986918, 1342177279}, // Organistrum
    {1395864371, 858993459, 1073741824, 322122547, 1181116006, 1610612735, 1181116006, 1342177279}, // Galop
    {1503238553, 751619276, 644245094, 536870912, 1288490188, 966367641, 1288490188, 447391711}, // Zanfona
    {1610612735, 1825361100, 966367641, 214748365, 1503238553, 1825361100, 1288490188, 447391711}, // Chien Fou
    {1932735282, 1181116006, 536870912, 0, 2147483647, 644245094, 858993459, 805306368}, // Rasp
    {322122547, 0, 1395864371, 644245094, 214748365, 0, 751619276, 1700091936}, // Whisper
    {1073741824, 0, 1825361100, 858993459, 858993459, 0, 2147483647, 89479201}, // Drone Hall
    {1717986918, 1503238553, 1503238553, 2147483647, 1717986918, 429496729, 1932735282, 984262622}, // Swarm
    {1073741824, 429496729, 1610612735, 644245094, 966367641, 536870912, 1503238553, 1700091936}, // Pilgrim
};
// Exercise all presets, mixed track controls, pitch changes and retriggers.
// Sound changes are allowed; enforce level bounds and audible output.
int main() {
    static hurdygurdy::Shared shared;
    hurdygurdy::Voice voices[8];
    opxy::i32 output[64], parameters[8];
    opxy::u32 pitch[64];
    for (unsigned preset = 0; preset < 12; ++preset) {
        std::memset(&shared, 0, sizeof(shared));
        std::memset(voices, 0, sizeof(voices));
        for (unsigned v = 0; v < 8; ++v)
            hurdygurdy::initialize(voices[v], shared, v + 1);
        double energy = 0;
        unsigned count = 0;
        for (unsigned block = 0; block < 1024; ++block) {
            for (unsigned p = 0; p < 8; ++p) parameters[p] = presets[preset][p];
            if (block >= 256 && block < 384) parameters[4] = 2147483647;
            if (block >= 384 && block < 512) parameters[0] = 0;
            if (block >= 512 && block < 640) parameters[3] = 0;
            if (block >= 640 && block < 768) parameters[1] = parameters[6] = 0;
            if (block >= 896) parameters[2] = parameters[7] = 2147483647;
            if (block % 128 == 0)
                hurdygurdy::start(voices[(block / 128) & 7], shared, 1 + ((block / 128) & 7));
            for (unsigned v = 0; v < 8; ++v) {
                for (unsigned i = 0; i < 64; ++i)
                    pitch[i] = 21426140u + v * 2000000u
                        + (block >= 768 ? ((block * 97u + i * 3101u) & 0xfffffu) : 0u);
                opxy::i32 own[8];
                for (unsigned p = 0; p < 8; ++p) own[p] = parameters[p];
                if (v & 1) own[2] = presets[(preset + 1) % 12][2];
                if (v % 3 == 0) own[4] = presets[(preset + 2) % 12][4];
                hurdygurdy::render(voices[v], shared, v + 1, output, own, pitch);
                for (unsigned i = 0; i < 64; ++i) {
                    if (output[i] < -644245094 || output[i] > 644245094) {
                        std::fprintf(stderr, "Preset %u exceeds declared peak\n", preset);
                        return 1;
                    }
                    const double sample = output[i] / 2147483648.0;
                    energy += sample * sample;
                    ++count;
                }
            }
        }
        if (energy / count < 1e-8) {
            std::fprintf(stderr, "Preset %u is silent\n", preset);
            return 1;
        }
    }
}
