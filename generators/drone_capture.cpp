// Offline source for the comparison wave bank. Not a machine source file.
#include "strings.cpp"
#include "string_samples.cpp"
#include <opxy/q31.hpp>
#include <cstdio>

int main() {
    using namespace hurdygurdy;
    for (u32 tonic = 0; tonic < 12; ++tonic) {
        for (u32 role = 0; role < 3; ++role) {
            for (u32 colour = 0; colour < 2; ++colour) {
                const i32 rosin = colour ? 2147483647 : 0;
                Bowing b = {};
                b.speed = 13958644 + opxy::q31::multiply(1073741824, 111669150);
                b.slope = (1234803098 - opxy::q31::multiply(rosin, 912680550)) / 65536;
                b.side = (251255586 - opxy::q31::multiply(1073741824, 125627793)) / 1048576;
                b.centre = 31948 - b.side * 2;
                u32 period = hg_bourdon_period[tonic] / rate_factor;
                period /= role == 0 ? 1u : role == 1 ? 3u : 4u;
                const u32 periods[4] = {period, period, period, period};
                String s = {};
                i32 bow[render_frames], out[render_frames];
                for (u32 i = 0; i < render_frames; ++i) bow[i] = b.speed >> 16;
                for (u32 block = 0; block < 2304; ++block) {
                    bow_string(s, out, bow, periods, 117, b, render_frames);
                    if (block >= 2048)
                        for (u32 i = 0; i < render_frames; ++i)
                            std::printf("%ld\n", static_cast<long>(out[i]));
                }
            }
        }
    }
}
