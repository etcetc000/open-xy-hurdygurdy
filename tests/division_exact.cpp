#include "control_division.cpp"
#include <cstdio>
int main() {
    using namespace hurdygurdy;
    for (u32 divisor = 64; divisor <= 0xffffffu; ++divisor) {
        const u32 got = divide_phase_period(divisor), expected = 0xffffffffu / divisor;
        if (got != expected) {
            std::fprintf(stderr, "Pitch divisor %lu: got %lu, expected %lu\n",
                         static_cast<unsigned long>(divisor), static_cast<unsigned long>(got),
                         static_cast<unsigned long>(expected));
            return 1;
        }
    }
    for (u32 divisor = 768; divisor <= 64000; ++divisor) {
        const u32 got = divide_window_weight(divisor), expected = 0x80000000u / divisor;
        if (got != expected) {
            std::fprintf(stderr, "Window divisor %lu: got %lu, expected %lu\n",
                         static_cast<unsigned long>(divisor), static_cast<unsigned long>(got),
                         static_cast<unsigned long>(expected));
            return 1;
        }
    }
    for (u32 divisor = 64708; divisor <= 65535; ++divisor) {
        if (detune_step(divisor) != ((65536u-divisor)<<16)/divisor) {
            std::fprintf(stderr, "Detune quotient mismatch at %lu\n", static_cast<unsigned long>(divisor));
            return 1;
        }
    }
    std::puts("Exact quotients for all 16,777,152 pitch, 63,233 window and 828 detune divisors");
}
