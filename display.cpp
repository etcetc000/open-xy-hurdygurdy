#include "display.h"
#include <opxy/q31.hpp>
#include "tables.h"

// The Hurdy Gurdy screen: a top view of the instrument in greys. Brighter
// grey means more of a control.
//   Wheel   crank and wheel rim turn faster       Rosin  dust around the wheel
//   Buzz    the chien on the trompette lights and jumps; Coup kicks it
//   Body    the soundbox outline grows; sound holes glow as notes start
//   Drone   drone strings brighten (whiter while the white knob is on Drone)
//   Tonic   the drone key's letter, top right
//   Unison  a second melody string beside the first
// The melody string draws the Helmholtz motion of a bowed string: a single
// kink running between wheel and nut along a parabola.

namespace hurdygurdy {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

enum Node : u32 {
    melody = 0, twin = 1, rim = 2, crank_arm = 3,
    body = 4, hole_a = 5, hole_b = 6, tailpiece = 7, opening = 8, keybox = 9, head = 10, nut = 11,
    melody_ends = 12, twin_ends = 13, bourdon = 14, mouche = 15, trompette = 16,
    key_first = 17, chien = 25, spark_first = 26, dust_first = 30, axle = 36, handle = 37,
    letter_first = 38, flat = 45,
};
enum Shade : u32 {
    black = 0, string_white = 1, string_grey = 2, line_white = 3, line_light = 4, line_mid = 5,
    line_dark = 6, line_faint = 7, solid_white = 8, solid_light = 9, solid_mid = 10, solid_dark = 11,
    body_dim = 12, body_bright = 13, hole_glow = 14, hole_dim = 15,
};

const i32 axle_x = 28, axle_y = 120;
const i32 rim_x = 88, rim_y = 120;
const i32 string_x = 264;               // centre of the 256 px moving string (path scale 2)
const i32 melody_y = 112, twin_y = 119;
const i32 chien_x = 156, chien_y = 150;
const i32 expired = 8192;               // Q10 seconds

// Tonic C Db D Eb E F Gb G Ab A Bb B -> letter (C D E F G A B) and flat.
const u32 tonic_letter[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
const u32 tonic_flat[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
// Rosin dust around the wheel opening, pixels.
const opxy::i16 dust[6][2] = { {70, 92}, {106, 100}, {68, 136}, {108, 142}, {72, 114}, {104, 122} };

inline i32 sine(u32 phase) { return hg_sine[(phase >> 8) & 255]; }          // Q10, 65536/turn
inline i32 control(const opxy::draw::Input& input, u32 slot) {
    return clamp(input.slots[slot], 0, opxy::q31::maximum);
}
inline i32 tenths(i32 value) { return value >> 21; }                         // Q31 -> 0..1023
inline void at(Frame& frame, u32 node, i32 x, i32 y, u32 shade, Paint paint = fill_and_stroke) {
    place(frame, node, x * unit, y * unit, unit, shade, paint);
}

// A bowed string's Helmholtz shape: two straight segments meeting at a kink.
// `travel` 0..65535 is the kink's place on its round trip; out and back trace
// the two halves of the lens.
inline void draw_string(Frame::path_type& path, u32 travel, i32 amplitude) {
    const u32 trip = travel & 65535u;
    // These are the only geometry inputs (amplitude is bounded to 6144).
    // Stable keys avoid native path rewrites when motion is paused.
    const u32 key = 1u + trip + (static_cast<u32>(amplitude) << 16);
    if (path.key == key) return;
    const u32 along = trip < 32768u ? trip * 2u : (65535u - trip) * 2u;  // 0..65535
    const i32 lens = static_cast<i32>((along * (65536u - along)) >> 20); // 0..1024
    i32 height = (amplitude * lens) >> 10;
    if (trip >= 32768u) height = -height;
    const i32 kink_x = -64 * unit + static_cast<i32>(along * 2u);
    for (u32 i = 0; i < 16; ++i) {
        const i32 t = static_cast<i32>(i);
        path.points[i].x = -64 * unit + ((kink_x + 64 * unit) * t) / 16;
        path.points[i].y = (height * t) / 16;
        path.points[16 + i].x = kink_x + ((64 * unit - kink_x) * t) / 16;
        path.points[16 + i].y = height - (height * t) / 16;
    }
    path.points[31].x = 64 * unit;
    path.points[31].y = 0;
    for (u32 i = 0; i < 32; ++i) path.points[32 + i] = path.points[31 - i];   // retrace
    path.key = key;
}

// The wheel's rim through its opening: a spine with ridges that scroll down.
inline void draw_rim(Frame::path_type& path, u32 phase) {
    const i32 offset = static_cast<i32>((phase >> 13) & 7u);   // one ridge every 8 px
    const u32 key = 1 + static_cast<u32>(offset);
    if (path.key == key) return;
    u32 n = 0;
    path.points[n++] = { 7 * unit, -38 * unit };
    for (i32 k = 0; k < 9; ++k) {
        const i32 y = (-36 + k * 8 + offset) * unit;
        path.points[n++] = { 7 * unit, y };
        path.points[n++] = { -7 * unit, y };
        path.points[n++] = { 7 * unit, y };
    }
    path.points[n++] = { 7 * unit, 38 * unit };
    const u32 forward = n;
    for (u32 i = 0; i < forward && n < Frame::point_count; ++i) path.points[n++] = path.points[forward - 1 - i];
    for (; n < Frame::point_count; ++n) path.points[n] = path.points[0];
    path.key = key;
}

// The crank turns about the shaft, so from above its handle swings up and down.
inline i32 draw_crank(Frame::path_type& path, u32 phase) {
    const i32 swing = floor_q10(sine(phase) * 24);
    const u32 key = 1 + static_cast<u32>(swing + 64);
    if (path.key == key) return swing;
    const Point points[4] = { { 5 * unit, 0 }, { -6 * unit, 0 }, { -6 * unit, swing * unit }, { -16 * unit, swing * unit } };
    u32 n = 0;
    for (u32 i = 0; i < 4; ++i) path.points[n++] = points[i];
    for (u32 i = 3; i-- > 0;) path.points[n++] = points[i];
    for (; n < Frame::point_count; ++n) path.points[n] = points[0];
    path.key = key;
    return swing;
}
}  // namespace

inline void display_reset(DisplayState& state) {
    state.initialized = state.track = state.note_serial = 0;
    state.wheel_phase = state.kink_phase = state.twin_offset = state.coup_phase = 0;
    state.frame = state.next_key = state.spare = 0;
    state.activity = state.pulse = 0;
    for (u32 k = 0; k < 8; ++k) state.key_age[k] = expired;
}

// Screen work only: no audio state, allocation or clock. Note starts arrive as
// a serial snapshot; several between frames count as one.
inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    for (u32 i = 0; i < opxy::page_controls; ++i) header(frame, i, "");
    const i32 wheel = control(input, 0), buzz = control(input, 1), size = control(input, 2);
    const i32 unison = control(input, 3), rosin = control(input, 4), coup = control(input, 5);
    const i32 drone = control(input, 6);
    u32 tonic = (static_cast<u32>(control(input, 7) >> 16) * 12u) >> 15;
    if (tonic > 11) tonic = 11;
    const bool still = input.reduced_motion != 0;
    const i32 elapsed = still ? 0 : clamp(input.elapsed, 0, 102);
    const bool drone_knob = input.shown[2] == 6;
    const bool tonic_knob = input.shown[3] == 7;

    // ---- Notes, wheel strokes and decays ----
    if (!state.initialized || state.track != input.track) {
        display_reset(state);
        state.initialized = 1;
        state.activity = 256;
    } else if (input.note_serial != state.note_serial && !still) {
        state.activity = unit;
        state.key_age[state.next_key & 7] = 0;
        state.next_key = (state.next_key * 5u + 3u) & 7u;   // keys go down in a loose pattern
        if (coup > 0) {
            const i32 strength = coup >= 858993459 ? unit : tenths(coup) * 5 / 2;
            if (strength > state.pulse) state.pulse = strength;
            state.coup_phase = 0;
        }
    }
    state.track = input.track;
    state.note_serial = input.note_serial;
    state.activity -= (state.activity * elapsed) >> 11;      // halves in about 1.4 s
    if (state.activity < 256) state.activity = 256;
    if (coup > 858993459 && !still) {
        // About the engine's 1.5 .. 9 Hz repeating coups.
        const u32 rate = static_cast<u32>(1536 + tenths(coup - 858993459) * 13);   // Q10 Hz
        const u32 before = state.coup_phase;
        state.coup_phase += static_cast<u32>(elapsed) * rate / 16u;                // 65536 per stroke
        if ((state.coup_phase & 65535u) < (before & 65535u) && state.pulse < 819) state.pulse = 819;
    }
    state.pulse -= (state.pulse * elapsed * 16) >> 10;      // ~65 ms
    if (state.pulse < 0) state.pulse = 0;
    for (u32 k = 0; k < 8; ++k) state.key_age[k] = clamp(state.key_age[k] + elapsed, 0, expired);
    ++state.frame;

    // ---- Soundbox: grows with Body, holes glow as notes start ----
    const i32 scale = 963 + (tenths(size) * 123 >> 10);      // Q10 0.94 .. 1.06
    place(frame, body, 132 * unit, 120 * unit, scale, size > 1073741824 ? body_bright : body_dim, fill_and_stroke);
    const u32 glow = state.activity > 560 ? hole_glow : hole_dim;
    at(frame, hole_a, 150, 72, glow);
    at(frame, hole_b, 150, 168, glow);
    at(frame, tailpiece, 0, 0, solid_dark, keep);
    at(frame, opening, 0, 0, line_mid, keep);
    at(frame, keybox, 0, 0, line_mid, keep);
    at(frame, head, 0, 0, line_mid, keep);
    at(frame, nut, 0, 0, line_light, keep);

    // ---- Wheel and crank: speed follows Wheel ----
    const i32 speed = 256 + (tenths(wheel) * 3 >> 1);       // Q10 turns per second, 0.25 .. 1.75
    state.wheel_phase += static_cast<u32>(elapsed * speed) / 16u;
    draw_rim(frame.paths[2], state.wheel_phase * 4u);
    at(frame, rim, rim_x, rim_y, wheel > 1288490188 ? line_white : line_light, stroke);
    const i32 swing = draw_crank(frame.paths[3], state.wheel_phase);
    at(frame, crank_arm, axle_x, axle_y, line_light, stroke);
    at(frame, axle, axle_x, axle_y, solid_mid);
    at(frame, handle, axle_x - 16, axle_y + swing, solid_white);
    const u32 specks = static_cast<u32>((tenths(rosin) * 7) >> 10);   // 0..6
    for (u32 i = 0; i < 6; ++i) {
        if (i >= specks) { hide(frame, dust_first + i); continue; }
        const i32 drift = still ? 0 : static_cast<i32>((state.frame + i) & 1);
        at(frame, dust_first + i, dust[i][0], dust[i][1] + drift, i < 3 ? solid_light : solid_mid);
    }

    // ---- Keys go down as notes start ----
    for (u32 k = 0; k < 8; ++k) {
        const i32 age = state.key_age[k];
        at(frame, key_first + k, 0, age < 120 ? 2 : 0, age < 120 ? solid_white : age < 360 ? solid_mid : solid_dark);
    }

    // ---- Drone strings: brighter with Drone, whiter while the white knob edits it ----
    const i32 d = tenths(drone);
    u32 drone_shade = d < 4 ? line_faint : d < 360 ? line_dark : d < 720 ? line_mid : line_light;
    if (drone_knob) drone_shade = drone_shade == line_faint ? line_dark : drone_shade - 1;
    const bool hum = d >= 4 && !still;
    for (u32 s = 0; s < 3; ++s) {
        const i32 wobble = hum && ((state.frame + s) & 1) ? 1 : 0;
        at(frame, bourdon + s, 0, wobble, drone_shade, stroke);
    }

    // ---- Melody string and its unison twin ----
    const u32 kink_rate = 640 + static_cast<u32>(tenths(wheel));        // Q10 trips per second
    state.kink_phase += static_cast<u32>(elapsed) * kink_rate / 16u;
    const i32 amplitude = ((2 * unit + 4 * state.activity) * (512 + (tenths(wheel) >> 1))) >> 10;
    draw_string(frame.paths[0], state.kink_phase, amplitude);
    place(frame, melody, string_x * unit, melody_y * unit, 2 * unit, string_white, stroke);
    at(frame, melody_ends, 0, 0, line_mid, stroke);
    if (unison > 21474836) {
        state.twin_offset += static_cast<u32>(elapsed) * static_cast<u32>(tenths(unison) + 40) / 4u;
        draw_string(frame.paths[1], state.kink_phase + state.twin_offset, amplitude);
        place(frame, twin, string_x * unit, twin_y * unit, 2 * unit, unison > 1073741824 ? string_white : string_grey, stroke);
        at(frame, twin_ends, 0, 0, line_dark, stroke);
    } else {
        draw_string(frame.paths[1], 0, 0);
        hide(frame, twin);
        hide(frame, twin_ends);
    }

    // ---- Chien: steady lift above half Buzz, jumps with each coup ----
    const i32 b = tenths(buzz);
    const i32 steady = b > 460 ? (b - 460) * 2 : 0;
    const i32 sensitivity = b >= 410 ? unit : b * 5 / 2;
    i32 lift = steady + ((state.pulse * sensitivity) >> 10);
    if (lift > unit) lift = unit;
    if (b < 4) lift = 0;
    const i32 jitter = lift > 300 && !still ? static_cast<i32>(state.frame & 1) * 2 - 1 : 0;
    at(frame, chien, chien_x + jitter, chien_y, b < 4 ? solid_dark : lift > 300 ? solid_white : solid_mid);
    for (u32 i = 0; i < 4; ++i) {
        if (lift > 400 && !still && ((state.frame + i) & 3) != 0) at(frame, spark_first + i, chien_x, chien_y, line_white, stroke);
        else hide(frame, spark_first + i);
    }

    // ---- Tonic letter: whiter while the orange knob edits it ----
    const u32 letter_shade = tonic_knob ? line_white : drone_knob || d >= 4 ? line_light : line_dark;
    for (u32 k = 0; k < 7; ++k) {
        if (k == tonic_letter[tonic]) at(frame, letter_first + k, 444, 52, letter_shade, stroke);
        else hide(frame, letter_first + k);
    }
    if (tonic_flat[tonic]) at(frame, flat, 466, 40, letter_shade, stroke);
    else hide(frame, flat);
}
}  // namespace hurdygurdy
