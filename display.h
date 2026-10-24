#pragma once
#include <opxy/draw.hpp>

namespace hurdygurdy {
// Four outlines (melody string, twin string, wheel rim, crank arm) and 46
// nodes in all; 16 grey palette entries. Matches resources.display.
using Frame = opxy::draw::Frame<4, 64, 46, 0, 16>;
struct DisplayState {
    opxy::u32 initialized, track, note_serial;
    opxy::u32 wheel_phase;   // 65536 per turn
    opxy::u32 kink_phase;    // Helmholtz kink, 65536 per round trip
    opxy::u32 twin_offset;   // unison beating
    opxy::u32 coup_phase;
    opxy::u32 frame, next_key, spare;
    opxy::i32 activity;      // Q10, rises on each note
    opxy::i32 pulse;         // Q10, coup stroke
    opxy::i32 key_age[8];    // Q10 seconds since each key went down
};
}
