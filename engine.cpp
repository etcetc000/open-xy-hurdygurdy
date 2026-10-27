#include "core.h"
#include "bowed_string.h"
#include <opxy/q31.hpp>
#include "tables.h"

// Hurdy Gurdy: a bowed-string machine.
//
// Each note bows a melody string (the chanterelle) with a friction model
// of the rosined wheel. Unison adds a detuned copy of that string. Three drone strings
// (bourdon, mouche, trompette) are bowed once for the whole machine by the
// newest note, so chords never stack drones and the drones carry on from note
// to note. The trompette rattles the chien (the buzzing bridge) when the
// wheel is stroked. Everything passes through a three-mode soundbox.
//
// Fixed point throughout: Q15 audio, Q31 control envelopes, Q12 delays; 32-bit arithmetic
// only (no 64-bit helpers are linked on the device).

namespace hurdygurdy {
namespace {
// Audio scaling uses arithmetic right shifts (floor) instead of signed
// division by powers of two: one instruction instead of a sign fix-up.
static_assert((-3 >> 1) == -2, "arithmetic right shift required");

using opxy::q31::multiply;

const i32 maximum = 2147483647;
const u32 q12 = 4096;
const u32 longest_period = 250u * q12;  // quarter-rate samples; 44.1 Hz lower bound
const u32 shortest_period = 3u * q12;    // about 3.7 kHz
const u32 main_position = 113;           // wheel contact, Q10 fraction of the string (0.11)
const u32 drone_position = 117;
const u32 segment = 4;                 // pitch updates every 16 device samples

// Control slots in definition.json order: the four encoders (blue, green,
// white, orange), then one alternate per encoder: Rosin, Coup, Drone, Tonic.
enum { wheel_slot, buzz_slot, body_slot, unison_slot, rosin_slot, coup_slot, drone_slot, tonic_slot };

inline i32 normalized(i32 value) { return value < 0 ? 0 : value; }
inline i32 clamp(i32 value, i32 low, i32 high) { return value < low ? low : value > high ? high : value; }
inline i32 magnitude(i32 value) { return value < 0 ? -value : value; }

// Saturating add: amplitudes stay well inside Q31, this only guards extremes.
inline i32 add(i32 a, i32 b) {
    const i32 sum = static_cast<i32>(static_cast<u32>(a) + static_cast<u32>(b));
    if (((a ^ sum) & (b ^ sum)) < 0) return a < 0 ? -maximum : maximum;
    return sum;
}

// Linear interpolation through a 33-entry table indexed by a Q31 control.
template <class T>
inline T lookup33(const T* table, i32 control) {
    const u32 index = static_cast<u32>(control) >> 26;
    const i32 fraction = static_cast<i32>((static_cast<u32>(control) & 0x3ffffffu) << 5);
    if (index >= 32) return table[32];
    const i32 low = static_cast<i32>(table[index]);
    const i32 high = static_cast<i32>(table[index + 1]);
    return static_cast<T>(low + multiply(high - low, fraction));
}

struct Controls {
    i32 wheel, drone, buzz, body, rosin, coup, unison;
    u32 tonic;
    explicit Controls(const i32* parameters) {
        wheel = normalized(parameters[wheel_slot]);
        drone = normalized(parameters[drone_slot]);
        buzz = normalized(parameters[buzz_slot]);
        body = normalized(parameters[body_slot]);
        rosin = normalized(parameters[rosin_slot]);
        coup = normalized(parameters[coup_slot]);
        unison = normalized(parameters[unison_slot]);
        // Same quantization as the stock enum path: Q31 -> Q15 -> twelve keys.
        tonic = (static_cast<u32>(normalized(parameters[tonic_slot]) >> 16) * 12u) >> 15;
        if (tonic > 11) tonic = 11;
    }
};

inline Bowing bowing_of(const Controls& c) {
    Bowing b;
    b.speed = 13958644 + multiply(c.wheel, 111669150);     // 0.0065 .. 0.0585
    b.slope = (1234803098 - multiply(c.rosin, 912680550)) / 65536;   // slope 4.6 .. 1.2
    b.side = (251255586 - multiply(c.wheel, 125627793)) / 1048576;     // quarter-rate loss magnitude correction
    b.centre = 31948 - b.side - b.side;                   // DC gain 0.975
    b.roughness = (64424509 + multiply(c.rosin, 429496730)) / 65536; // 0.03 .. 0.23
    return b;
}

// Wheel noise: Q31 LCG/filter state, Q15 output, about 220 Hz at quarter rate.
inline i32 roughen(u32& random, i32& noise) {
    random = random * 1664525u + 1013904223u;
    const i32 white = static_cast<i32>(random);
    noise += (white >> 3) - (noise >> 3);
    return noise >> 17;
}

// Period in Q12 samples from a phase increment; one 32-bit division.
inline u32 period_of(u32 increment) {
    u32 divisor = increment >> 8;
    if (divisor < 64) divisor = 64;
    u32 period = (0xffffffffu / divisor) << 2;   // quarter-rate period in Q12
    while (period > longest_period) period >>= 1;
    return period < shortest_period ? shortest_period : period;
}

inline void decimate(String& s, i32* out, const i32* samples) {
    // Polyphase form of the same seven-tap half-band FIR. Keep the four
    // required history values in registers instead of indexing an eight-word
    // ring for every output sample. Coefficients and rounding are unchanged.
    i32 even = s.decimate[0], odd1 = s.decimate[1];
    i32 odd2 = s.decimate[2], odd3 = s.decimate[3];
    for (u32 i = 0; i < 16; ++i) {
        const i32 odd = samples[2*i+1];
        const i32 value = (-1475 * (odd + odd3) + 9667 * (odd1 + odd2)
                           + 16384 * even) / 32768;
        out[i] = clamp(value, -32767, 32767);
        even = samples[2*i];
        odd3 = odd2;
        odd2 = odd1;
        odd1 = odd;
    }
    s.decimate[0] = static_cast<opxy::i16>(even);
    s.decimate[1] = static_cast<opxy::i16>(odd1);
    s.decimate[2] = static_cast<opxy::i16>(odd2);
    s.decimate[3] = static_cast<opxy::i16>(odd3);
}

// Melody strings run at 22.05 kHz, then a seven-tap half-band filter
// decimates to the 11.025 kHz mixing/body rate. No oscillator replacement.
inline void melody_string(String& s, i32* out, const i32* bow, const u32* periods, u32 position, const Bowing& b) {
    i32 faster_bow[32], samples[32];
    u32 faster_periods[4];
    for (u32 i = 0; i < 16; ++i) faster_bow[2*i] = faster_bow[2*i+1] = bow[i];
    for (u32 i = 0; i < 4; ++i) faster_periods[i] = periods[i] * 2;
    Bowing faster = b;
    faster.side *= 4;
    faster.centre = 31948 - faster.side - faster.side;
    bow_string(s, samples, faster_bow, faster_periods, position, faster, 32);
    decimate(s, out, samples);
}

// Pitch-synchronous delay ramps: the two read heads are one input period
// apart, reducing cancellation and pitch sidebands during their crossfade.
// The main string still supplies the physical bow and harmonic evolution.
inline void detuned_copy(DetunedCopy& s, i32* out, const i32* input, i32 ratio, const u32* periods) {
    const u32 key = static_cast<u32>(ratio);
    if (s.ratio_key != key) {
        const u32 denominator = key >> 15;
        s.step = ((65536u - denominator) << 16) / denominator;
        s.ratio_key = key;
    }
    u32 phase = s.phase, at = s.position;
    for (u32 part = 0; part < 4; ++part) {
        const u32 window = periods[part] << 5; // two Q12 periods -> Q16
        if (s.period_key != periods[part]) {
            s.weight_scale = 0x80000000u / (window >> 9);
            s.period_key = periods[part];
            if (phase >= window) phase %= window;
        }
        for (u32 i = part * 4; i < part * 4 + 4; ++i) {
            s.history[at & 511u] = static_cast<opxy::i16>(input[i]);
            u32 opposite = phase + window / 2;
            if (opposite >= window) opposite -= window;
            const i32 a = tap(s.history, 511u, at, (phase >> 4) + 8192u);
            const i32 b = tap(s.history, 511u, at, (opposite >> 4) + 8192u);
            const u32 distance = phase < window / 2 ? phase : window - phase;
            const i32 weight = static_cast<i32>(((distance >> 8) * s.weight_scale) >> 16);
            out[i] = b + (((a - b) * weight) >> 15);
            phase = phase >= s.step ? phase - s.step : phase + window - s.step;
            ++at;
        }
    }
    s.phase = phase;
    s.position = at;
}

// Wheel speed with roughness. Noise and smoothing advance every fourth
// sample and hold between: the roughness is low-passed near 220 Hz, so a
// 2.76 kHz update keeps its character for a quarter of the work.
const u32 rough_step = 4;
inline void wheel(i32* bow, i32& smooth, i32 target, u32 shift, u32& random, i32& noise, i32 roughness) {
    for (u32 i = 0; i < render_frames; i += rough_step) {
        for (u32 k = 0; k < rough_step; ++k) smooth += (target - smooth) >> shift;  // same per-sample glide
        random = random * 1664525u + 1013904223u;
        noise += (static_cast<i32>(random) >> 1) - (noise >> 1);   // ~220 Hz at the 2.76 kHz update
        const i32 rough = noise >> 17;
        const i32 speed = smooth >> 16;
        const i32 value = speed + ((speed * ((rough * roughness) >> 13)) >> 15);
        for (u32 k = 0; k < rough_step; ++k) bow[i + k] = value;
    }
}

// ---- Drones and chien ----------------------------------------------------

const i32 drone_bow_limit = 150323855;   // 0.07: keeps coup surges inside range
const i32 coup_decay = 2144489075;       // 65 ms wrist-stroke envelope
const i32 note_coup_share = 858993459;   // coup 0..0.4: note-start strokes only
const i32 rattle_b0 = 183390350;          // chien resonator, 2.2 kHz, Q 3 (halved)
const i32 rattle_a1 = 543188326;
const i32 rattle_a2 = 706961124;

// Renders the drone block into shared.drone_block with this voice's controls.
inline void render_drone(Shared& shared, const Controls& c, const Bowing& b, bool stroke,
                         i32* bow, i32* work) {
    DroneSet& d = shared.drone;
    const u32 bourdon = (hg_bourdon_period[c.tonic] / rate_factor);
    const u32 bourdon_periods[4] = { bourdon, bourdon, bourdon, bourdon };
    const u32 mouche = bourdon / 3u;     // a twelfth above: just fifth, no beating
    const u32 mouche_periods[4] = { mouche, mouche, mouche, mouche };
    const u32 trompette = bourdon / 4u;  // two octaves above
    const u32 trompette_periods[4] = { trompette, trompette, trompette, trompette };

    // Smoothed controls.
    const i32 level_target = c.drone;
    const i32 buzz_target = c.buzz;

    // Coup: note-start strokes in the first part of the knob, then repeating
    // strokes from 1.5 to 9 Hz, restarted by each note so they follow the
    // sequence.
    const bool repeating = c.coup > note_coup_share;
    u32 rate = 0;
    if (repeating) {
        const i32 over = c.coup - note_coup_share;          // 0 .. 0.6
        rate = lookup33(hg_coup_rate, clamp(over + multiply(over, 1431655765), 0, maximum));
    }
    if (stroke && c.coup > 0) {
        const i32 strength = repeating ? maximum : c.coup + c.coup + c.coup / 2;
        if (strength > d.coup) d.coup = strength;
        d.coup_phase = 0;
    }

    i32 level = d.level;
    if (level_target == 0 && d.level < 64) d.level = level = 0;
    const bool tonal = level_target > 0 || d.level > 0;
    if (tonal) {
        // Bourdon and mouche share one bow speed.
        wheel(bow, d.bow[0], b.speed, 8, d.random, d.noise, b.roughness);
        level = d.level;
        bow_string(d.bourdon, work, bow, bourdon_periods, drone_position, b, render_frames);
        for (u32 i = 0; i < render_frames; ++i) {
            level += (level_target - level) >> 6;
            shared.drone_block[i] = (work[i] * (((level >> 16) * 3) >> 2)) >> 15;      // 0.75
        }
        bow_string(d.mouche, work, bow, mouche_periods, drone_position, b, render_frames);
        level = d.level;
        for (u32 i = 0; i < render_frames; ++i) {
            level += (level_target - level) >> 6;
            shared.drone_block[i] += (work[i] * (((level >> 16) * 18022) >> 15)) >> 15;      // 0.55
        }
    } else {
        for (u32 i = 0; i < render_frames; ++i) shared.drone_block[i] = 0;
    }

    // Trompette: the current coup surges its bow speed (held for the block).
    const i32 surge = 1073741824 + multiply(d.coup, 966367642);   // 1 .. 1.9, halved
    for (u32 i = 0; i < render_frames; i += rough_step) {
        for (u32 k = 0; k < rough_step; ++k) d.bow[2] += (b.speed - d.bow[2]) >> 8;
        const i32 pushed = clamp(multiply(d.bow[2], surge) * 2, 0, drone_bow_limit);
        d.random = d.random * 1664525u + 1013904223u;
        d.noise += (static_cast<i32>(d.random) >> 1) - (d.noise >> 1);
        const i32 speed = pushed >> 16;
        const i32 value = speed + ((speed * (((d.noise >> 17) * b.roughness) >> 13)) >> 15);
        for (u32 k = 0; k < rough_step; ++k) bow[i + k] = value;
    }
    bow_string(d.trompette, work, bow, trompette_periods, drone_position, b, render_frames);

    // Chien: the trompette lifts its buzzing bridge when the wheel is stroked
    // (a coup) and, above half Buzz, all the time. Impacts come from the peak
    // of each swing, so the buzz is pitched with the trompette.
    i32 buzz = d.buzz;
    const bool chien = buzz_target > 0 || buzz > 0;
    // Steady lift above Buzz 0.45, full at 1; coup sensitivity full by 0.4.
    const i32 steady = buzz_target <= 966367642 ? 0
        : add(buzz_target - 966367642, buzz_target - 966367642);
    const i32 sensitivity = buzz_target >= 858993459 ? maximum : buzz_target + buzz_target + buzz_target / 2;
    level = d.level;
    for (u32 i = 0; i < render_frames; ++i) {
        level += (level_target - level) >> 6;
        buzz += (buzz_target - buzz) >> 6;
        if (rate) {
            const u32 before = d.coup_phase;
            d.coup_phase += rate * rate_factor;
            if (d.coup_phase < before && d.coup < 1717986918) d.coup = 1717986918;  // 0.8
        }
        d.coup = multiply(d.coup, coup_decay);
        const i32 t = work[i];
        i32 out = (t * (level >> 17)) >> 15;                          // 0.50
        if (chien) {
            const i32 lift_target = add(steady, multiply(d.coup, sensitivity));
            d.lift += (lift_target - d.lift) >> 3;
            d.envelope += (magnitude(t) - d.envelope) >> 4;
            i32 peak = t - (d.envelope >> 1);
            if (peak < 0) peak = 0;
            const i32 impact = peak - d.rectified;
            d.rectified = peak;
            const i32 rough = roughen(d.random, d.noise);
            const i32 jagged = clamp(impact + ((impact * (rough * 2)) >> 15), -32767, 32767);
            const i32 rattle = clamp(((rattle_b0 / 131072) * jagged + (rattle_a1 / 131072) * d.rattle_1
                                - (rattle_a2 / 131072) * d.rattle_2) >> 13, -32767, 32767);
            d.rattle_2 = d.rattle_1;
            d.rattle_1 = rattle;
            // The rasp is loud on a real gurdy: lift the resonator 30 dB.
            const i32 loudness = 1073741824 + (buzz >> 1);                  // 0.5 .. 1 with Buzz
            const i32 chatter = clamp((((rattle * (d.lift >> 16)) >> 15) * (loudness >> 16)) >> 15, -204, 204);
            out += chatter * 16;                                   // peak 0.1
        }
        shared.drone_block[i] = clamp(shared.drone_block[i] + out, -32767, 32767);
    }
    d.level = level;
    d.buzz = buzz;
}

// ---- Soundbox and output ----------------------------------------------

const i32 dc_pole = 2123145451;       // 20 Hz
const i32 output_limit = 644245094;   // 0.3: the declared peak, under the half-scale ceiling

// Output gain from the measured Wheel x Rosin loudness grid (Q29), so the two
// knobs mostly change character rather than volume.
inline i32 level_of(const Controls& c) {
    const u32 w = static_cast<u32>(c.wheel) >> 29, r = static_cast<u32>(c.rosin) >> 29;
    const i32 wf = static_cast<i32>((static_cast<u32>(c.wheel) & 0x1fffffffu) << 2);
    const i32 rf = static_cast<i32>((static_cast<u32>(c.rosin) & 0x1fffffffu) << 2);
    const i32* row = hg_level + w * 5;
    const i32 low = row[r] + multiply(row[r + 1] - row[r], rf);
    const i32 high = row[r + 5] + multiply(row[r + 6] - row[r + 5], rf);
    return low + multiply(high - low, wf);
}

// Three body modes (air, nasal, edge) plus the direct string, then a DC
// blocker and the output level, in one pass.
inline void soundbox(Voice& voice, Shared& shared, i32* signal, const Controls& c) {
    const i32 body = c.body;
    const u32 body_key = static_cast<u32>(body) + 1u;
    i32* const k = shared.body_coefficients;
    if (shared.body_key != body_key) {
        const u32 index = static_cast<u32>(body) >> 27;   // 0..15
        const i32 fraction = static_cast<i32>((static_cast<u32>(body) & 0x7ffffffu) << 4);
        for (u32 j = 0; j < 9; ++j) {
            const i32 low = hg_body[index * 9 + j];
            k[j] = low + multiply(hg_body[(index + 1) * 9 + j] - low, fraction);
        }
        // Mode gains grow 0.8x .. 1.2x with size around 0.45 air, 0.50 nasal, 0.40 edge.
        const i32 g0 = 773094113 + multiply(body, 386547057);
        const i32 g1 = 858993459 + multiply(body, 429496730);
        const i32 g2 = 687194767 + multiply(body, 343597384);
        shared.body_direct = 1181116006 - multiply(body, 322122547);      // 0.55 .. 0.40
        // Fold each mode's gain into its input coefficient: three fewer multiplies
        // per sample. Quantize the resulting coefficients to Q13 for bounded
        // 32-bit sums with the Q15 resonator state.
        k[0] = multiply(k[0], g0);
        k[3] = multiply(k[3], g1);
        k[6] = multiply(k[6], g2);
        for (u32 j = 0; j < 9; ++j) k[j] /= 131072;
        shared.body_direct /= 65536;
        shared.body_key = body_key;
    }
    const u32 wheel_key = static_cast<u32>(c.wheel) + 1u;
    const u32 rosin_key = static_cast<u32>(c.rosin) + 1u;
    if (shared.level_wheel_key != wheel_key || shared.level_rosin_key != rosin_key) {
        shared.output_gain = level_of(c) / 262144;
        shared.level_wheel_key = wheel_key;
        shared.level_rosin_key = rosin_key;
    }
    // Q15 signal/state, Q13 mode coefficients and output gain. The largest
    // absolute coefficient sum is below 32768, so these products fit i32.
    // Coefficients copied to locals: stores to `signal` could otherwise alias
    // the shared table and force a reload of every coefficient per sample.
    const i32 direct = shared.body_direct, level = shared.output_gain;
    const i32 k0 = k[0], k1 = k[1], k2 = k[2], k3 = k[3], k4 = k[4], k5 = k[5];
    const i32 k6 = k[6], k7 = k[7], k8 = k[8];
    const i32 pole = dc_pole >> 16;
    i32 in_1 = voice.body_1, in_2 = voice.body_2;
    i32 a1 = voice.mode_1[0], a2 = voice.mode_2[0];
    i32 n1 = voice.mode_1[1], n2 = voice.mode_2[1];
    i32 e1 = voice.mode_1[2], e2 = voice.mode_2[2];
    i32 dc_in = voice.dc_in, dc_out = voice.dc_out;
    for (u32 i = 0; i < render_frames; ++i) {
        const i32 in = signal[i] >> 1;
        const i32 difference = in - in_2;
        i32 mode = clamp((k0 * difference + k1 * a1 - k2 * a2) >> 13, -32767, 32767);
        a2 = a1; a1 = mode;
        mode = clamp((k3 * difference + k4 * n1 - k5 * n2) >> 13, -32767, 32767);
        n2 = n1; n1 = mode;
        mode = clamp((k6 * difference + k7 * e1 - k8 * e2) >> 13, -32767, 32767);
        e2 = e1; e1 = mode;
        in_2 = in_1; in_1 = in;
        const i32 shaped = clamp(((in * direct) >> 15) + a1 + n1 + e1, -32767, 32767);
        dc_out = clamp(shaped - dc_in + ((dc_out * pole) >> 15), -32767, 32767);
        dc_in = shaped;
        signal[i] = clamp((dc_out * level) >> 10, -9830, 9830);
    }

    voice.body_1 = in_1; voice.body_2 = in_2;
    voice.mode_1[0] = a1; voice.mode_2[0] = a2;
    voice.mode_1[1] = n1; voice.mode_2[1] = n2;
    voice.mode_1[2] = e1; voice.mode_2[2] = e2;
    voice.dc_in = dc_in; voice.dc_out = dc_out;
}

const i32 click_decay = 1323854692; // 3 ms, one device block

}  // namespace

void initialize(Voice& voice, Shared& shared, u32 owner) {
    voice.slot = 0;
    voice.random = owner * 2654435761u + 12345u;
    voice.drone_gain = 0;
    voice.drone_serial = shared.drone_serial;
    voice.output_previous = 0;
    voice.pitch_period = 0; // zero is not a valid cached period
    start(voice, shared, owner);
}

// A note: lease the melody strings (kept when this voice still holds them, so
// repeated notes on one voice stay legato), ask for the drones and a coup.
void start(Voice& voice, Shared& shared, u32 owner) {
    const u32 held = voice.slot;
    voice.slot = shared.melody.acquire(owner, voice.slot);
    if (voice.slot != held) voice.bow = 0;   // fresh strings: the wheel bites anew
    voice.claim_pending = 1;
    voice.stroke_pending = 1;
    voice.click = 536870912;  // 0.25
}

void destroy(Voice& voice, Shared& shared, u32 owner) {
    shared.melody.release(owner, voice.slot);
    voice.slot = 0;
    if (shared.drone_owner == owner) shared.drone_owner = 0;
    voice.drone_gain = 0;
}

static void render_internal(Voice& voice, Shared& shared, u32 owner, opxy::Output output,
            opxy::Parameters parameters, opxy::Pitch pitch) {
    const Controls c(parameters);

    // ---- Who bows the drones: the newest note that wants them ----
    // A sounding note also takes over when the owner stopped rendering (its
    // note ended): no owner block arrived since this voice last looked.
    const bool wants_drones = c.drone > 4294967 || c.buzz > 4294967;   // above 0.2%
    const bool owner_gone = shared.drone_owner != owner && shared.drone_serial == voice.drone_serial;
    if (wants_drones && (voice.claim_pending || shared.drone_owner == 0 || owner_gone)) shared.drone_owner = owner;
    voice.claim_pending = 0;
    const bool owns = wants_drones && shared.drone_owner == owner;
    if (!wants_drones && shared.drone_owner == owner) shared.drone_owner = 0;
    const bool stroke = voice.stroke_pending != 0;
    voice.stroke_pending = 0;

    MelodySlot* slot = shared.melody.get(owner, voice.slot);
    if (!slot && !owns && voice.drone_gain == 0) {
        // Strings lent to a newer note and no drones to hand over: no work.
        voice.output_previous = 0;
        for (u32 i = 0; i < render_frames; ++i) output[i] = 0;
        return;
    }

    const Bowing b = bowing_of(c);
    i32 bow[render_frames];
    i32 work[render_frames];

    // ---- Melody strings ----
    if (slot) {
        wheel(bow, voice.bow, b.speed, 7, voice.random, voice.noise, b.roughness);   // bites over ~12 ms
        u32 periods[4];
        for (u32 part = 0; part < 4; ++part) {
            const u32 increment = pitch[part * segment * rate_factor];
            if (!voice.pitch_period || increment != voice.pitch_increment) {
                voice.pitch_increment = increment;
                voice.pitch_period = period_of(increment);
            }
            periods[part] = voice.pitch_period;
        }
        melody_string(slot->main, output, bow, periods, main_position, b);
        // The twin joins fully by 12% Unison; above that the knob only detunes.
        const i32 twin_target = c.unison <= 21474836 ? 0
            : multiply(c.unison >= 268435456 ? maximum : c.unison * 8, 1825361101);  // up to 0.85
        // Integer smoothing stalls at 1..63. End that inaudible tail so an
        // unison string switched off does not consume CPU for the whole note.
        if (twin_target == 0 && voice.twin_gain < 64) voice.twin_gain = 0;
        if (twin_target > 0 || voice.twin_gain > 0) {
            if (voice.twin_gain == 0) slot->twin = DetunedCopy{};
            const i32 ratio = lookup33(hg_twin_ratio, c.unison);
            detuned_copy(slot->twin, work, output, ratio, periods);
            if ((twin_target - voice.twin_gain) / 64 == 0) {
                // Once integer smoothing has settled, these are block constants.
                const i32 gain = voice.twin_gain;
                const i32 attenuation = static_cast<i32>(static_cast<u32>(gain) / 3u);
                for (u32 i = 0; i < render_frames; ++i) {
                    const i32 together = clamp(output[i] + ((work[i] * (gain >> 16)) >> 15), -32767, 32767);
                    output[i] = together - ((together * (attenuation >> 16)) >> 15);
                }
            } else {
                for (u32 i = 0; i < render_frames; ++i) {
                    voice.twin_gain += (twin_target - voice.twin_gain) >> 6;
                    const i32 together = clamp(output[i] + ((work[i] * (voice.twin_gain >> 16)) >> 15), -32767, 32767);
                    // The gain is nonnegative. Unsigned division by three uses
                    // constant arithmetic on Blackfin instead of a runtime helper.
                    const i32 attenuation = static_cast<i32>(static_cast<u32>(voice.twin_gain) / 3u);
                    output[i] = together - ((together * (attenuation >> 16)) >> 15);
                }
            }
        }
        // Key tangent: a short knock as the key meets the string.
        if (voice.click) {
            const i32 next = multiply(voice.click, click_decay);
            const i32 step = (next - voice.click) / static_cast<i32>(render_frames);
            for (u32 i = 0; i < render_frames; ++i) {
                voice.random = voice.random * 1664525u + 1013904223u;
                const i32 impulse = (static_cast<i32>(voice.random) / 262144)
                    * (voice.click / 65536) / 32768;
                output[i] = clamp(output[i] + impulse, -32767, 32767);
                voice.click += step;
            }
            voice.click = next < 2048 ? 0 : next;
        }

    } else {
        // The pool lent this voice's strings to a newer note: silent until restruck.
        for (u32 i = 0; i < render_frames; ++i) output[i] = 0;
    }

    // ---- Drones: rendered once per block by their owner, faded in and out ----
    bool fresh = shared.drone_serial != voice.drone_serial;   // owner rendered since we looked
    if (owns) {
        render_drone(shared, c, b, stroke, bow, work);
        ++shared.drone_serial;
        fresh = true;
    }
    voice.drone_serial = shared.drone_serial;
    const i32 drone_target = owns ? maximum : 0;
    if (drone_target > 0 || voice.drone_gain > 0) {
        for (u32 i = 0; i < render_frames; ++i) {
            voice.drone_gain += drone_target > voice.drone_gain
                ? (voice.drone_gain < maximum - 19479880 ? 19479880 : maximum - voice.drone_gain)  // 10 ms
                : (voice.drone_gain > 19479880 ? -19479880 : -voice.drone_gain);
            if (fresh) output[i] = clamp(output[i]
                + ((shared.drone_block[i] * (voice.drone_gain >> 16)) >> 15),
                -32767, 32767);
        }
    }

    soundbox(voice, shared, output, c);
}

// Interpolate the mixed/body output back to the device's 44.1 kHz rate.
void render(Voice& voice, Shared& shared, u32 owner, opxy::Output output,
            opxy::Parameters parameters, opxy::Pitch pitch) {
    i32 low[render_frames];
    render_internal(voice, shared, owner, low, parameters, pitch);
    // Keep all internal audio in Q15; expand only at the public Q31 boundary.
    i32 previous = voice.output_previous;
    for (u32 i = 0; i < render_frames; ++i) {
        const i32 current = low[i] * 65536;
        const i32 difference = current - previous;
        const i32 quarter = difference >> 2;
        output[4*i] = previous + quarter;
        output[4*i+1] = previous + (difference >> 1);
        output[4*i+2] = current - quarter;
        output[4*i+3] = current;
        previous = current;
    }
    voice.output_previous = previous;
}

}  // namespace hurdygurdy
