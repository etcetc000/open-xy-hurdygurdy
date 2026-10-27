// Deterministic offline refactoring trace. Link the machine's declared sources.
#include "engine.cpp"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::FILE* file = std::fopen(argv[1], "wb");
    if (!file) return 3;
    using namespace hurdygurdy;
    static Shared shared;
    Voice voices[8] = {};
    for (u32 v = 0; v < 8; ++v) initialize(voices[v], shared, v+1);
    u32 random = 12345, pitch[64];
    i32 parameters[8], output[64];
    for (u32 block = 0; block < 4096; ++block) {
        for (u32 v = 0; v < 8; ++v) {
            for (u32 p = 0; p < 8; ++p) {
                random = random*1664525u+1013904223u;
                parameters[p] = static_cast<i32>(random>>1);
            }
            // Long off spans exercise full Unison draining and fresh clears.
            if ((block/128)&1u) parameters[3] = 0;
            for (u32 part = 0; part < 4; ++part) {
                random = random*1664525u+1013904223u;
                for (u32 i = part*16; i < part*16+16; ++i)
                    pitch[i] = block%8 ? random : (part&1 ? 0xffffffffu : 0u);
            }
            if (block%97==0) start(voices[v], shared, v+1);
            render(voices[v], shared, v+1, output, parameters, pitch);
            if (std::fwrite(output,sizeof(output),1,file)!=1) return 4;
        }
    }
    if (std::fwrite(&shared,sizeof(shared),1,file)!=1 ||
        std::fwrite(voices,sizeof(voices),1,file)!=1) return 4;
    return std::fclose(file)==0 ? 0 : 5;
}
