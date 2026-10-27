#include "drone_tone.cpp"
#include "tables.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using namespace hurdygurdy;
    double largest = 0;
    for (u32 tonic = 0; tonic < 12; ++tonic) {
        for (u32 role = 0; role < 3; ++role) {
            DroneTone tone = {};
            tone.phase = tone.position = 0xfffffff0u;
            i32 bow[drone_frames], out[drone_frames];
            for (u32 i = 0; i < drone_frames; ++i) bow[i] = 1064;
            for (u32 block = 0; block < 512; ++block)
                drone_tone(tone, out, bow, tonic, role, 1073741824, 1073741824);
            double samples[8192];
            for (u32 block = 0; block < 8192/drone_frames; ++block) {
                drone_tone(tone, out, bow, tonic, role, 1073741824, 1073741824);
                for (u32 i = 0; i < drone_frames; ++i) samples[block*drone_frames+i] = out[i];
            }
            const u32 period = (hg_bourdon_period[tonic] / drone_rate_factor) / (role == 0 ? 1 : role == 1 ? 3 : 4);
            const double expected = period / 4096.0;
            const int centre = static_cast<int>(expected + 0.5);
            double correlations[7];
            int best = 0;
            for (int j = 0; j < 7; ++j) {
                const int lag = centre + j - 3;
                double cross = 0, a = 0, b = 0;
                for (int i = 0; i < 8192-lag; ++i) {
                    cross += samples[i]*samples[i+lag];
                    a += samples[i]*samples[i]; b += samples[i+lag]*samples[i+lag];
                }
                require(a > 1000 && b > 1000, "Silent drone");
                correlations[j] = cross/std::sqrt(a*b);
                if (correlations[j] > correlations[best]) best = j;
            }
            require(best > 0 && best < 6, "Drone pitch outside search range");
            const double offset = 0.5*(correlations[best-1]-correlations[best+1]) /
                (correlations[best-1]-2*correlations[best]+correlations[best+1]);
            const double measured = centre + best - 3 + offset;
            const double cents = std::fabs(1200*std::log(measured/expected)/std::log(2.0));
            if (cents > largest) largest = cents;
            require(cents < 8, "Drone waveform tuning exceeds eight cents");
            // Abrupt control and bow extremes exercise the interpolation,
            // clamp and filter bounds, including phase/position wrap above.
            for (u32 block = 0; block < 2048; ++block) {
                for (u32 i = 0; i < drone_frames; ++i) bow[i] = (block & 1) ? 0 : 4096;
                drone_tone(tone, out, bow, tonic, role,
                           (block & 2) ? 2147483647 : 0, (block & 4) ? 2147483647 : 0);
                for (u32 i = 0; i < drone_frames; ++i)
                    require(out[i] >= -32767 && out[i] <= 32767, "Drone output exceeds Q15");
            }
        }
    }
    std::printf("36 drone pitches and control transitions passed; largest error %.3f cents\n", largest);
}
