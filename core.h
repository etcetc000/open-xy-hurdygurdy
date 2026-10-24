#pragma once
#include <opxy/audio.hpp>
#include <opxy/lease_pool.hpp>

namespace hurdygurdy {
using opxy::i32;
using opxy::u32;

// One bowed string: two delay lines meeting at the wheel (the "bow").
// The neck line runs from the wheel to the nut, the bridge line from the
// wheel to the bridge. Lengths fit notes down to about 44 Hz; lower notes
// fold up an octave. Waves are stored as 16-bit to halve memory and cache
// traffic; loop arithmetic is Q15 with 32-bit intermediates. Melody runs
// at half the device rate; drones and the body run at quarter rate.
const u32 rate_factor = 4;
const u32 render_frames = opxy::block_frames / rate_factor;
const u32 neck_size = 512;
const u32 bridge_size = 64;
struct String {
    opxy::i16 neck[neck_size];
    opxy::i16 bridge[bridge_size];
    u32 position;      // next write index for both rings
    i32 loss_1, loss_2; // Q15 bridge loss filter history
    u32 neck_delay;    // current delays, Q12 samples
    u32 bridge_delay;
    opxy::i16 decimate[8]; // melody half-band history; unused by drones
    u32 decimate_at;
    u32 primed;
};

// A melody voice's strings: the chanterelle and its unison twin.
struct MelodySlot {
    String main;
    String twin;
};

// The drone strings play once for the whole machine (see render_drone).
struct DroneSet {
    String bourdon;
    String mouche;
    String trompette;
    i32 bow[3];          // smoothed bow velocity per string
    i32 noise;           // wheel roughness, low-passed
    u32 random;
    i32 envelope;        // Q15 trompette amplitude follower
    i32 lift;            // how far the chien is lifted, Q31
    i32 rectified;       // Q15 last rectified trompette sample
    i32 rattle_1, rattle_2; // Q15 chien resonator
    i32 coup;            // wrist-stroke envelope
    u32 coup_phase;
    i32 level;           // smoothed drone level
    i32 buzz;            // smoothed chien level
};

// At most eight melody notes sound at once across every track; a ninth takes
// the oldest note's strings. This also caps the engine's worst-case CPU.
const u32 melody_slots = 8;
struct Shared {
    opxy::LeasePool<MelodySlot, melody_slots> melody;
    DroneSet drone;
    i32 drone_block[render_frames];
    u32 drone_owner;     // voice that bows the drones; 0 when none
    u32 drone_serial;    // bumped each time the owner renders a block
    // Last soundbox controls, shared across serial voice callbacks. Keys are
    // normalized Q31 values plus one; zero means the cache is uninitialized.
    u32 body_key, level_wheel_key, level_rosin_key;
    i32 body_coefficients[9], body_direct, output_gain;
};

struct Voice {
    u32 slot;            // melody lease
    u32 claim_pending;   // take over the drones on this note
    u32 stroke_pending;  // a note-start coup for the chien
    u32 random;
    i32 bow;             // smoothed bow velocity
    i32 noise;
    i32 twin_gain;
    i32 drone_gain;      // crossfade when drones move between notes
    u32 drone_serial;
    i32 click;           // key tangent envelope
    i32 body_1, body_2;  // Q15 soundbox input history
    i32 mode_1[3], mode_2[3];
    i32 dc_in, dc_out;
    i32 output_previous;
    u32 pitch_increment, pitch_period; // exact last pitch conversion, shared by segments
};

void initialize(Voice&, Shared&, u32 owner);
void start(Voice&, Shared&, u32 owner);
void destroy(Voice&, Shared&, u32 owner);
void render(Voice&, Shared&, u32 owner, opxy::Output, opxy::Parameters, opxy::Pitch);
}  // namespace hurdygurdy
