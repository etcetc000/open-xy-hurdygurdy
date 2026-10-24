#include "engine.cpp"
#include <cstdio>

int main() {
    const opxy::i16 endpoints[] = {-32768, -32767, -16384, -1, 0, 1, 16384, 32766, 32767};
    opxy::i16 line[4] = {};
    for (auto a : endpoints) for (auto b : endpoints) {
        line[1] = a;
        line[0] = b;
        for (opxy::u32 fraction = 0; fraction < 4096; ++fraction) {
            const opxy::i64 expected = static_cast<opxy::i64>(a) +
                (static_cast<opxy::i64>(b) - a) * fraction / 4096;
            const opxy::i32 actual = hurdygurdy::tap(line, 3, 2, 4096 + fraction);
            if (actual != expected) {
                std::fprintf(stderr, "Fractional delay mismatch at %d, %d, %u\n", a, b, fraction);
                return 1;
            }
        }
    }
    std::puts("Q15/Q12 interpolation matches the wide reference at all tested endpoints/fractions");
}
