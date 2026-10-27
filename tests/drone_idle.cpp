#include "engine.cpp"
#include "control_division.cpp"
#include "chien.cpp"
#include "drone_tone.cpp"
#include "strings.cpp"
#include "string_samples.cpp"
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    static hurdygurdy::Shared shared;
    hurdygurdy::Voice voice = {};
    opxy::i32 output[64], parameters[8] = {};
    opxy::u32 pitch[64];
    for (unsigned i = 0; i < 64; ++i) pitch[i] = 42852281u;
    parameters[0] = parameters[4] = 1073741824;
    parameters[1] = 2147483647; // Buzz needs trompette, but no tonal drones.
    hurdygurdy::initialize(voice, shared, 1);
    for (unsigned block = 0; block < 100; ++block)
        hurdygurdy::render(voice, shared, 1, output, parameters, pitch);
    require(shared.drone.bourdon.position == 0 && shared.drone.mouche.position == 0,
            "Inaudible tonal drones consume processing time");
    require(shared.drone.trompette.position != 0, "Buzz lost its excitation oscillator");
    parameters[6] = 2147483647;
    for (unsigned block = 0; block < 100; ++block)
        hurdygurdy::render(voice, shared, 1, output, parameters, pitch);
    require(shared.drone.bourdon.position != 0 && shared.drone.mouche.position != 0,
            "Tonal drones did not resume");
    parameters[6] = 0;
    for (unsigned block = 0; block < 1000; ++block)
        hurdygurdy::render(voice, shared, 1, output, parameters, pitch);
    require(shared.drone.level == 0, "Drone fade stalls above zero");
    const unsigned bourdon = shared.drone.bourdon.position, mouche = shared.drone.mouche.position;
    const unsigned trompette = shared.drone.trompette.position;
    hurdygurdy::render(voice, shared, 1, output, parameters, pitch);
    require(shared.drone.bourdon.position == bourdon && shared.drone.mouche.position == mouche,
            "Faded-out tonal drones keep rendering");
    require(shared.drone.trompette.position != trompette, "Buzz stopped with Drone");
    std::puts("Drone idle, fade and resume checks passed");
}
