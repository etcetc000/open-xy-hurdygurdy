#include "core.h"
#include <opxy/q31.hpp>
#include "tables.h"

// Hurdy Gurdy: a bowed-string machine.
//
// Each note bows a melody string (the chanterelle, plus an optional unison
// twin) with a friction model of the rosined wheel. Three drone strings
// (bourdon, mouche, trompette) are bowed once for the whole machine by the
// newest note, so chords never stack drones and the drones carry on from note
// to note. The trompette rattles the chien (the buzzing bridge) when the
// wheel is stroked. Everything passes through a three-mode soundbox.
//
// Fixed point throughout: Q31 signals, Q12 delay lengths, 32-bit arithmetic
// only (no 64-bit helpers are linked on the device).

namespace hurdygurdy {
namespace {

using opxy::q31::multiply;

const i32 maximum = 2147483647;
const u32 neck_mask = neck_size - 1;
const u32 bridge_mask = bridge_size - 1;
const u32 q12 = 4096;
const u32 longest_period = 1000u * q12;  // 44.1 Hz; lower notes fold up an octave
const u32 shortest_period = 12u * q12;   // about 3.7 kHz
const u32 main_position = 113;           // wheel contact, Q10 fraction of the string (0.11)
const u32 twin_position = 128;           // 0.125
const u32 drone_position = 117;
const u32 segment = 16;                  // pitch updates every 16 samples

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

// How the wheel meets the string: bow speed, friction curve and string loss.
struct Bowing {
    i32 speed;   // target bow velocity, Q31 (a quarter of STK's units)
    i32 slope;   // friction curve slope / 8, Q31
    i32 side;    // bridge loss filter: side taps and centre tap
    i32 centre;
    i32 roughness; // relative wheel noise
    explicit Bowing(const Controls& c) {
        speed = 13958644 + multiply(c.wheel, 111669150);     // 0.0065 .. 0.0585
        slope = 1234803098 - multiply(c.rosin, 912680550);   // slope 4.6 .. 1.2
        side = 251255586 - multiply(c.wheel, 125627793);     // taps 0.117 .. 0.059: brighter when faster
        centre = 2093796557 - side - side;                   // DC gain 0.975
        roughness = 64424509 + multiply(c.rosin, 429496730); // 0.03 .. 0.23
    }
};

// Wheel noise: white LCG noise through a one-pole low-pass (about 220 Hz).
inline i32 roughen(u32& random, i32& noise) {
    random = random * 1664525u + 1013904223u;
    const i32 white = static_cast<i32>(random);
    noise += white / 32 - noise / 32;
    return noise;
}

// Delay tap `delay` (Q12) samples behind the next write position, linearly
// interpolated. Lines hold Q15 words; taps return Q31.
inline i32 tap(const opxy::i16* line, u32 mask, u32 position, u32 delay) {
    const u32 whole = delay >> 12;
    const i32 fraction = static_cast<i32>((delay & 4095u) << 19);
    const i32 a = static_cast<i32>(line[(position - whole) & mask]) * 65536;
    const i32 b = static_cast<i32>(line[(position - whole - 1u) & mask]) * 65536;
    return a + multiply(b - a, fraction);
}

inline i32 tap_whole(const opxy::i16* line, u32 mask, u32 position, u32 whole) {
    return static_cast<i32>(line[(position - whole) & mask]) * 65536;
}

inline opxy::i16 store(i32 value) { return static_cast<opxy::i16>(value / 65536); }

// Period in Q12 samples from a phase increment; one 32-bit division.
inline u32 period_of(u32 increment) {
    u32 divisor = increment >> 8;
    if (divisor < 64) divisor = 64;
    u32 period = (0xffffffffu / divisor) << 4;   // Q8 quotient, widened to Q12
    while (period > longest_period) period >>= 1;
    return period < shortest_period ? shortest_period : period;
}

// Bow one string for a block. `periods` holds four segment periods (Q12),
// `bow` the per-sample bow velocity. Writes the wave arriving at the bridge.
// This loop is the engine's cost: about 2 taps, 5 multiplies and a table
// lookup per sample, with the string's state held in registers.
inline void bow_string(String& s, i32* out, const i32* bow, const u32* periods, u32 position_q10,
                       const Bowing& b) {
    opxy::i16* const neck = s.neck;
    opxy::i16* const bridge = s.bridge;
    u32 at = s.position;
    i32 loss_1 = s.loss_1, loss_2 = s.loss_2;
    u32 neck_now = s.neck_delay, bridge_now = s.bridge_delay;
    const i32 side = b.side, centre = b.centre, slope = b.slope;
    for (u32 part = 0; part < opxy::block_frames / segment; ++part) {
        // Loop delay is neck + bridge + one sample for the symmetric loss filter.
        const u32 total = periods[part] - q12;
        // The bridge side takes whole samples; the neck carries the fraction,
        // so one interpolated tap per sample keeps the tuning exact.
        u32 bridge_delay = ((total >> 10) * position_q10) & ~4095u;
        if (bridge_delay < q12) bridge_delay = q12;
        const u32 neck_delay = total - bridge_delay;
        if (!s.primed) {
            neck_now = neck_delay;
            bridge_now = bridge_delay;
            s.primed = 1;
        }
        bridge_now = bridge_delay;
        const u32 bridge_whole = bridge_delay >> 12;
        const i32 neck_step = (static_cast<i32>(neck_delay) - static_cast<i32>(neck_now)) / static_cast<i32>(segment);
        i32* const block = out + part * segment;
        const i32* const push = bow + part * segment;
        for (u32 n = 0; n < segment; ++n) {
            neck_now = static_cast<u32>(static_cast<i32>(neck_now) + neck_step);
            const i32 arriving = tap_whole(bridge, bridge_mask, at, bridge_whole);
            const i32 lossy = multiply(side, add(arriving, loss_2)) + multiply(centre, loss_1);
            loss_2 = loss_1;
            loss_1 = arriving;
            const i32 from_bridge = -lossy;
            const i32 from_nut = -tap(neck, neck_mask, at, neck_now);
            const i32 slip = add(push[n], -add(from_bridge, from_nut));
            // Friction: the string sticks to the wheel near zero slip, slides beyond.
            const i32 scaled = multiply(magnitude(slip), slope);
            const u32 index = static_cast<u32>(scaled) >> 20;
            i32 friction = hg_bow[256];
            if (index < 256)
                friction = hg_bow[index] + multiply(hg_bow[index + 1] - hg_bow[index],
                                                    static_cast<i32>((static_cast<u32>(scaled) & 0xfffffu) << 11));
            const i32 injected = multiply(slip, friction);
            neck[at & neck_mask] = store(add(from_bridge, injected));
            bridge[at & bridge_mask] = store(add(from_nut, injected));
            ++at;
            block[n] = arriving;
        }
        neck_now = neck_delay;
        bridge_now = bridge_delay;
    }
    s.position = at;
    s.loss_1 = loss_1;
    s.loss_2 = loss_2;
    s.neck_delay = neck_now;
    s.bridge_delay = bridge_now;
}

// ---- Drones and chien ----------------------------------------------------

const i32 drone_bow_limit = 150323855;   // 0.07: keeps coup surges inside range
const i32 coup_decay = 2146734613;       // 65 ms wrist-stroke envelope
const i32 note_coup_share = 858993459;   // coup 0..0.4: note-start strokes only
const i32 rattle_b0 = 53262563;          // chien resonator, 2.2 kHz, Q 3 (halved)
const i32 rattle_a1 = 1938869378;
const i32 rattle_a2 = 967216698;

// Renders the drone block into shared.drone_block with this voice's controls.
inline void render_drone(Shared& shared, const Controls& c, const Bowing& b, bool stroke,
                         i32* bow, i32* work) {
    DroneSet& d = shared.drone;
    const u32 bourdon = hg_bourdon_period[c.tonic];
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

    // Bourdon and mouche share one bow speed.
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        d.bow[0] += (b.speed - d.bow[0]) / 1024;
        const i32 rough = roughen(d.random, d.noise);
        bow[i] = d.bow[0] + multiply(d.bow[0], multiply(rough, b.roughness) * 4);
    }
    i32 level = d.level;
    bow_string(d.bourdon, work, bow, bourdon_periods, drone_position, b);
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        level += (level_target - level) / 256;
        shared.drone_block[i] = multiply(work[i], multiply(level, 1610612736));      // 0.75
    }
    bow_string(d.mouche, work, bow, mouche_periods, drone_position, b);
    level = d.level;
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        level += (level_target - level) / 256;
        shared.drone_block[i] += multiply(work[i], multiply(level, 1181116006));      // 0.55
    }

    // Trompette: the current coup surges its bow speed (held for the block).
    const i32 surge = 1073741824 + multiply(d.coup, 966367642);   // 1 .. 1.9, halved
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        d.bow[2] += (b.speed - d.bow[2]) / 1024;
        const i32 pushed = clamp(multiply(d.bow[2], surge) * 2, 0, drone_bow_limit);
        const i32 rough = roughen(d.random, d.noise);
        bow[i] = pushed + multiply(pushed, multiply(rough, b.roughness) * 4);
    }
    bow_string(d.trompette, work, bow, trompette_periods, drone_position, b);

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
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        level += (level_target - level) / 256;
        buzz += (buzz_target - buzz) / 256;
        if (rate) {
            const u32 before = d.coup_phase;
            d.coup_phase += rate;
            if (d.coup_phase < before && d.coup < 1717986918) d.coup = 1717986918;  // 0.8
        }
        d.coup = multiply(d.coup, coup_decay);
        const i32 t = work[i];
        i32 out = multiply(t, multiply(level, 1073741824));                          // 0.50
        if (chien) {
            const i32 lift_target = add(steady, multiply(d.coup, sensitivity));
            d.lift += (lift_target - d.lift) / 32;
            d.envelope += (magnitude(t) - d.envelope) / 64;
            i32 peak = t - d.envelope / 2;
            if (peak < 0) peak = 0;
            const i32 impact = peak - d.rectified;
            d.rectified = peak;
            const i32 rough = roughen(d.random, d.noise);
            const i32 jagged = add(impact, multiply(impact, add(rough, rough)));
            const i32 rattle = (multiply(rattle_b0, jagged) + multiply(rattle_a1, d.rattle_1)
                                - multiply(rattle_a2, d.rattle_2)) * 2;
            d.rattle_2 = d.rattle_1;
            d.rattle_1 = rattle;
            // The rasp is loud on a real gurdy: lift the resonator 30 dB.
            const i32 loudness = 1073741824 + buzz / 2;                  // 0.5 .. 1 with Buzz
            const i32 chatter = clamp(multiply(multiply(rattle, d.lift), loudness), -13421772, 13421772);
            out = add(out, chatter * 16);                                   // peak 0.1
        }
        shared.drone_block[i] = add(shared.drone_block[i], out);
    }
    d.level = level;
    d.buzz = buzz;
}

// ---- Soundbox and output ----------------------------------------------

const i32 dc_pole = 2141373067;       // 20 Hz
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
inline void soundbox(Voice& voice, i32* signal, const Controls& c) {
    const i32 body = c.body;
    const u32 index = static_cast<u32>(body) >> 27;   // 0..15
    const i32 fraction = static_cast<i32>((static_cast<u32>(body) & 0x7ffffffu) << 4);
    i32 k[9];
    for (u32 j = 0; j < 9; ++j) {
        const i32 low = hg_body[index * 9 + j];
        k[j] = low + multiply(hg_body[(index + 1) * 9 + j] - low, fraction);
    }
    // Mode gains grow 0.8x .. 1.2x with size around 0.45 air, 0.50 nasal, 0.40 edge.
    const i32 g0 = 773094113 + multiply(body, 386547057);
    const i32 g1 = 858993459 + multiply(body, 429496730);
    const i32 g2 = 687194767 + multiply(body, 343597384);
    const i32 direct = 1181116006 - multiply(body, 322122547);      // 0.55 .. 0.40
    const i32 level = level_of(c);
    // Fold each mode's gain into its input coefficient: three fewer multiplies
    // per sample, same response (the resonators are linear).
    k[0] = multiply(k[0], g0);
    k[3] = multiply(k[3], g1);
    k[6] = multiply(k[6], g2);
    i32 in_1 = voice.body_1, in_2 = voice.body_2;
    i32 a1 = voice.mode_1[0], a2 = voice.mode_2[0];
    i32 n1 = voice.mode_1[1], n2 = voice.mode_2[1];
    i32 e1 = voice.mode_1[2], e2 = voice.mode_2[2];
    i32 dc_in = voice.dc_in, dc_out = voice.dc_out;
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        const i32 in = signal[i] / 2;                          // headroom for the resonances
        const i32 difference = add(in, -in_2);
        i32 half = add(multiply(k[0], difference), add(multiply(k[1], a1), -multiply(k[2], a2)));
        a2 = a1; a1 = add(half, half);
        half = add(multiply(k[3], difference), add(multiply(k[4], n1), -multiply(k[5], n2)));
        n2 = n1; n1 = add(half, half);
        half = add(multiply(k[6], difference), add(multiply(k[7], e1), -multiply(k[8], e2)));
        e2 = e1; e1 = add(half, half);
        in_2 = in_1; in_1 = in;
        const i32 shaped = add(add(multiply(in, direct), a1), add(n1, e1));
        dc_out = add(add(shaped, -dc_in), multiply(dc_out, dc_pole));
        dc_in = shaped;
        // Stock-like loudness: typical peaks near 0.22, like Macro Osc.
        i32 out = multiply(dc_out, level);                     // x level / 4
        out = add(out, out); out = add(out, out);
        out = add(out, out);                                   // x level x 2 (x1 net of the headroom)
        signal[i] = clamp(out, -output_limit, output_limit);
    }
    voice.body_1 = in_1; voice.body_2 = in_2;
    voice.mode_1[0] = a1; voice.mode_2[0] = a2;
    voice.mode_1[1] = n1; voice.mode_2[1] = n2;
    voice.mode_1[2] = e1; voice.mode_2[2] = e2;
    voice.dc_in = dc_in; voice.dc_out = dc_out;
}

const i32 click_decay = 2131312914;   // 3 ms

}  // namespace

void initialize(Voice& voice, Shared& shared, u32 owner) {
    voice.slot = 0;
    voice.random = owner * 2654435761u + 12345u;
    voice.drone_gain = 0;
    voice.drone_serial = shared.drone_serial;
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

void render(Voice& voice, Shared& shared, u32 owner, opxy::Output output,
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
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }

    const Bowing b(c);
    i32 bow[opxy::block_frames];
    i32 work[opxy::block_frames];

    // ---- Melody strings ----
    if (slot) {
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            voice.bow += (b.speed - voice.bow) / 512;   // the wheel bites over ~12 ms
            const i32 rough = roughen(voice.random, voice.noise);
            bow[i] = voice.bow + multiply(voice.bow, multiply(rough, b.roughness) * 4);
        }
        u32 periods[4];
        for (u32 part = 0; part < 4; ++part) periods[part] = period_of(pitch[part * segment]);
        bow_string(slot->main, output, bow, periods, main_position, b);
        // The twin joins fully by 12% Unison; above that the knob only detunes.
        const i32 twin_target = c.unison <= 21474836 ? 0
            : multiply(c.unison >= 268435456 ? maximum : c.unison * 8, 1825361101);  // up to 0.85
        // Integer smoothing stalls at 1..255. End that inaudible tail so an
        // unison string switched off does not consume CPU for the whole note.
        if (twin_target == 0 && voice.twin_gain < 256) voice.twin_gain = 0;
        if (twin_target > 0 || voice.twin_gain > 0) {
            const i32 ratio = lookup33(hg_twin_ratio, c.unison);
            u32 twin_periods[4];
            for (u32 part = 0; part < 4; ++part) {
                u32 period = static_cast<u32>(multiply(static_cast<i32>(periods[part]), ratio));
                twin_periods[part] = period < shortest_period ? shortest_period : period;
            }
            bow_string(slot->twin, work, bow, twin_periods, twin_position, b);
            for (u32 i = 0; i < opxy::block_frames; ++i) {
                voice.twin_gain += (twin_target - voice.twin_gain) / 256;
                const i32 together = add(output[i], multiply(work[i], voice.twin_gain));
                output[i] = together - multiply(together, voice.twin_gain / 3);
            }
        }
        // Key tangent: a short knock as the key meets the string.
        if (voice.click) {
            for (u32 i = 0; i < opxy::block_frames; ++i) {
                voice.random = voice.random * 1664525u + 1013904223u;
                output[i] = add(output[i], multiply(static_cast<i32>(voice.random) / 4, voice.click));
                voice.click = multiply(voice.click, click_decay);
            }
            if (voice.click < 2048) voice.click = 0;
        }
    } else {
        // The pool lent this voice's strings to a newer note: silent until restruck.
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
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
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            voice.drone_gain += drone_target > voice.drone_gain
                ? (voice.drone_gain < maximum - 4869970 ? 4869970 : maximum - voice.drone_gain)  // 10 ms
                : (voice.drone_gain > 4869970 ? -4869970 : -voice.drone_gain);
            if (fresh) output[i] = add(output[i], multiply(shared.drone_block[i], voice.drone_gain));
        }
    }

    soundbox(voice, output, c);
}

}  // namespace hurdygurdy
