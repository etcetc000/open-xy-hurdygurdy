// Offline A/B renderer. Compile against either machine's include directory.
#include "engine.cpp"
#include "control_division.cpp"
#include "strings.cpp"
#include "string_samples.cpp"
#include <cstdio>
#include <cmath>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::FILE* file = std::fopen(argv[1], "wb");
    if (!file) return 3;
    using namespace hurdygurdy;
    static Shared shared;
    Voice voice = {};
    initialize(voice, shared, 1);
    i32 parameters[8] = {}, output[64], bow[render_frames], work[render_frames];
    u32 pitch[64];
    const bool isolated = argv[2][0] == 'd';
    i32 previous = 0;
    unsigned written = 0;
    const unsigned frames = 44100*9;
    for (unsigned block = 0; written < frames; ++block) {
        const double t = written/44100.0;
        const double sweep = t < 3 ? 0.5 : t < 6 ? (t-3)/3 : 0.5;
        parameters[0] = static_cast<i32>((t < 3 ? 0.55 : t < 6 ? 0.2+0.7*sweep : 0.7)*2147483647.0);
        parameters[1] = t < 3 ? 0 : 1288490188; // plain drones, then Buzz
        parameters[2] = 1073741824;
        parameters[3] = 536870911;
        parameters[4] = static_cast<i32>(sweep*2147483647.0);
        parameters[5] = t < 6 ? 0 : 1717986917;
        parameters[6] = 1503238552;
        const unsigned tonic = t < 7 ? 0 : t < 8 ? 5 : 7;
        parameters[7] = static_cast<i32>((tonic+0.25)/12*2147483647.0);
        const unsigned note = static_cast<unsigned>(t/0.75)%4;
        const double freqs[4] = {261.625565,329.627557,391.995436,523.251131};
        const u32 increment = static_cast<u32>(freqs[note]*4294967296.0/44100+0.5);
        for (u32 i = 0; i < 64; ++i) pitch[i] = increment;
        if (isolated) {
            const Controls c(parameters);
            const Bowing b = bowing_of(c);
            render_drone(shared, c, b, block == 0, bow, work);
            for (u32 i = 0; i < render_frames; ++i) work[i] = shared.drone_block[i];
            soundbox(voice, shared, work, c);
            for (u32 i = 0; i < render_frames; ++i) {
                const i32 current = work[i]*65536, difference = current-previous;
                output[4*i] = previous+(difference>>2);
                output[4*i+1] = previous+(difference>>1);
                output[4*i+2] = current-(difference>>2);
                output[4*i+3] = current;
                previous = current;
            }
        } else {
            render(voice, shared, 1, output, parameters, pitch);
        }
        for (u32 i = 0; i < 64 && written < frames; ++i,++written) {
            double fade = 1;
            if (written < 441) fade = written/441.0;
            if (frames-written < 441) fade = (frames-written)/441.0;
            const opxy::i16 sample = static_cast<opxy::i16>((output[i]>>16)*fade);
            if (std::fwrite(&sample,sizeof(sample),1,file)!=1) return 4;
        }
    }
    return std::fclose(file)==0 ? 0 : 5;
}
