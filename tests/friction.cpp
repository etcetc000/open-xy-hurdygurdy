#include "engine.cpp"
#include <cmath>
#include <cstdio>

int main() {
    // Check the full curve, including the flat sticking/sliding regions.
    // Nearest-entry sampling permits <0.8 percentage points of coefficient error.
    for (unsigned step = 0; step <= 65536; ++step) {
        const double u = step / 16384.0;
        const double raw = std::pow(u + 0.75, -4.0);
        const double expected = raw < 0.01 ? 0.01 : raw > 0.98 ? 0.98 : raw;
        const unsigned index = (step + 32) / 64;
        const double actual = hurdygurdy::hg_bow15[index] / 32768.0;
        if (std::fabs(actual - expected) > 0.0081) {
            std::fprintf(stderr, "Friction error at u=%f: %f versus %f\n", u, actual, expected);
            return 1;
        }
        if (index && hurdygurdy::hg_bow15[index] > hurdygurdy::hg_bow15[index - 1]) {
            std::fprintf(stderr, "Friction curve is not monotonic\n");
            return 1;
        }
    }
    std::puts("Friction approximation remains within 0.0081 of the model");
}
