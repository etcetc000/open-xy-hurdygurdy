#include "engine.cpp"
#include "drone_tone.cpp"
#include "strings.cpp"
#include "string_samples.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

int main() {
    const unsigned samples = 65536;
    const double pi = 3.14159265358979323846;
    static double window[samples], signal[samples];
    for (unsigned i = 0; i < samples; ++i)
        window[i] = 0.5 - 0.5 * std::cos(2 * pi * i / (samples - 1));
    const double notes[] = {110, 220, 440, 523.251, 659.255, 783.991, 880, 1046.502, 1760};
    const opxy::i32 controls[] = {0, 1073741824, 2147483647};
    static hurdygurdy::Shared shared;
    double worst = 0;
    for (auto wheel : controls) for (auto rosin : controls) for (double frequency : notes) {
        std::memset(&shared, 0, sizeof(shared));
        hurdygurdy::Voice voice = {};
        opxy::i32 output[64], parameters[8] = {wheel, 0, 1073741824, 0, rosin, 0, 0, 0};
        opxy::u32 pitch[64];
        for (unsigned i = 0; i < 64; ++i)
            pitch[i] = static_cast<opxy::u32>(frequency * 4294967296.0 / 44100.0);
        hurdygurdy::initialize(voice, shared, 1);
        for (unsigned block = 0; block < 1024; ++block)
            hurdygurdy::render(voice, shared, 1, output, parameters, pitch);
        double energy = 0;
        for (unsigned block = 0; block < samples / 64; ++block) {
            hurdygurdy::render(voice, shared, 1, output, parameters, pitch);
            for (unsigned i = 0; i < 64; ++i) {
                const double value = output[i] / 2147483648.0;
                energy += value * value;
                signal[block * 64 + i] = value * window[block * 64 + i];
            }
        }
        if (energy / samples < 1e-8) {
            std::fprintf(stderr, "Silent tuning probe at %.3f Hz\n", frequency);
            return 1;
        }
        // A fine Goertzel scan of the fundamental, with a Hann window to
        // reject neighboring harmonics. Probe beyond the accepted +/-8 cents.
        double best = -1, cents = 0;
        for (int step = -24; step <= 24; ++step) {
            const double candidate = frequency * std::pow(2.0, step * 0.5 / 1200.0);
            const double coefficient = 2 * std::cos(2 * pi * candidate / 44100.0);
            double previous = 0, older = 0;
            for (unsigned i = 0; i < samples; ++i) {
                const double next = signal[i] + coefficient * previous - older;
                older = previous;
                previous = next;
            }
            const double power = previous * previous + older * older - coefficient * previous * older;
            if (power > best) { best = power; cents = step * 0.5; }
        }
        if (std::fabs(cents) > worst) worst = std::fabs(cents);
        if (std::fabs(cents) > 8) {
            std::fprintf(stderr, "Tuning %.3f Hz: %.1f cents, Wheel=%d Rosin=%d\n",
                         frequency, cents, wheel, rosin);
            return 1;
        }
    }
    std::printf("81 sustained melody probes passed; largest deviation %.1f cents\n", worst);
}
