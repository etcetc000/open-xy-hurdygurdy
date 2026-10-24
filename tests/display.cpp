#include "../display.cpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using namespace hurdygurdy;
    DisplayState state = {};
    hurdygurdy::Frame frame = {};
    opxy::draw::Input input = {};
    input.elapsed = 17;
    input.track = 1;
    input.note_serial = 42;
    input.reduced_motion = 1;
    for (unsigned i = 0; i < 8; ++i) input.slots[i] = 1073741824;
    display_reset(state);
    display(state, input, frame);
    require(opxy::draw::valid(frame), "Invalid initial frame");
    require(state.next_key == 0 && state.pulse == 0, "Initial display fabricated a note");
    hurdygurdy::Frame previous = frame;
    display(state, input, frame);
    require(std::memcmp(&previous, &frame, sizeof(frame)) == 0,
            "Reduced motion rewrites unchanged geometry");
    input.reduced_motion = 0;
    for (unsigned step = 0; step < 1000; ++step) {
        previous = frame;
        input.elapsed = step % 5 == 0 ? -1 : step % 7 == 0 ? 10000 : 17;
        for (unsigned p = 0; p < 8; ++p)
            input.slots[p] = static_cast<opxy::i32>((step * 2654435761u + p * 1664525u) & 0x7fffffffu);
        if (step % 11 == 0) ++input.note_serial;
        if (step % 37 == 0) ++input.track;
        display(state, input, frame);
        require(opxy::draw::valid(frame), "Invalid animated frame");
        for (unsigned p = 0; p < hurdygurdy::Frame::path_count; ++p)
            if (std::memcmp(previous.paths[p].points, frame.paths[p].points, sizeof(frame.paths[p].points)) != 0)
                require(previous.paths[p].key != frame.paths[p].key, "Changed geometry reused a cached key");
    }
    std::puts("Display: bounded frames, initial note snapshot, reduced motion and geometry keys passed");
}
