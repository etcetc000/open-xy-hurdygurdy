#pragma once
#include <opxy/draw.hpp>

namespace hurdygurdy {
// Four animated outlines, 46 instrument nodes and eight header steps;
// sixteen text slots and grey palette entries. Matches resources.display.
using Frame = opxy::draw::Frame<4, 64, 54, 16, 16>;
struct DisplayState {
    opxy::u32 initialized, track, note_serial;
    opxy::u32 wheel_phase;   // 65536 per turn
    opxy::u32 kink_phase;    // Helmholtz kink, 65536 per round trip
    opxy::u32 twin_offset;   // unison beating
    opxy::u32 coup_phase;
    opxy::u32 frame, next_key, spare;
    opxy::u32 header_page;
    opxy::i32 header_since;
    opxy::i32 activity;      // Q10, rises on each note
    opxy::i32 pulse;         // Q10, coup stroke
    opxy::i32 key_age[8];    // Q10 seconds since each key went down
};
}
