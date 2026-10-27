# Hurdy Gurdy implementation notes

This branch starts from PR #147's `b1366c21a9ccbc0221ba4d326461b0c9c5904bdb`.
Original authorship and distribution permission remain in PROVENANCE.md and LICENSE.
The public machine name, parameter order, encoder pairs and twelve presets are unchanged.
Pressing any encoder now switches both control rows together, using the Macro Osc
two-page header: fixed row order, active-row emphasis and lowercase captions.
Header transitions settle within 0.15 seconds; reduced motion and track changes
snap to the final layout. Display state is 88 bytes, with 54 nodes and 16 text slots.
This UI change uses SDK 7910129d7b390d4161899a5a4e45483ed01431ec for drawing above
the stock header. Earlier native cost counts below describe the locked audio
build; they are not a fresh cycle qualification of this newer SDK build.

## Sound and cost

The main melody string retains the physical bowed-string solver at 22.05 kHz.
The three shared drones are periodic table oscillators at 5.5125 kHz. Their
128-point waves come from the physical solver at both Rosin endpoints for all
12 tonics and three roles. Harmonic fitting removes DC and limits bandwidth;
wave phase continues across notes. Rosin blends two waveforms with smoothing,
bow velocity/noise modulates amplitude, and Wheel controls a brightness filter.
The chien still receives the trompette waveform and the existing Coup envelope.
The chien also runs at 5.5125 kHz with retuned resonator poles and envelope
decays; bow roughness retains its 2.76 kHz clock. The eight-sample drone mix
is interpolated to the 11.025 kHz soundbox rate. The wave bank excludes harmonics
above the new Nyquist limit. Drone attacks, brightness, evolution, level and
knob response intentionally change. The
physical melody and the existing Unison implementation are unchanged.
A symmetric, quantized three-tap loss filter uses shifts instead of coefficient
multiplies. It keeps one sample of filter delay and has 31/32 DC gain; damping
and rounding intentionally differ from the source version.

Unison is a detuned copy of the main string at 11.025 kHz, using two fractional
read heads crossfaded one pitch period apart. It replaces the second bowed-string
simulation. This shares the main string's excitation and adds pitch-dependent
latency, changing attack texture and beating. It is not sample-equivalent to the
physical twin. Unison still joins by 12% and detunes up to 22 cents. Its private
history is cleared when Unison resumes after being fully off.

All rendered waves remain Q15 with 32-bit intermediates. Fractional interpolation
remains in the physical solver and the detuned copy. Nearest-sample delay reads
were tried and rejected because they failed the existing eight-cent tuning limit.
A physical twin at 11.025 kHz was also tried; the detuned copy is the chosen
lower-cost implementation. Shared state is 18,256 bytes, with 92 bytes per voice.

Drone and Unison mixing, chien filtering and their envelope smoothing use
arithmetic right shifts for power-of-two scaling. This rounds negative values
down instead of toward zero. Rates, physical solver, fractional delays and filter
coefficients are unchanged by this arithmetic optimization; low-level rounding
and envelope convergence intentionally differ. The source's existing arithmetic
right-shift requirement remains explicit.

## Validation and device measurement

Machine tests cover control corners, pitch extremes, declared peak bounds,
24-note polyphony, stealing, retriggers, twelve presets, drone idle/resume, display,
friction and decimation. The 81-case melody tuning test retains its eight-cent
limit. The detuned-copy test measures 18 pitch/detune combinations. Transition tests
exercise changing pitch windows, signed sample bounds, unsigned position wrap,
constant-gain crossfades and complete draining after silence. The scalar solver
reference specifies the new filter's signed rounding independently. All 36 drone
frequencies are checked within eight cents, alongside phase wrapping and abrupt
control/bow changes. Host audio comparisons omit stock envelopes and filters;
matching input controls and output gain do not imply matching perceived loudness.

The SDK build must authenticate stock/tool inputs, reconstruct the package and
pass full native release verification. Firmware and captures stay outside every
checkout. Host timing does not establish device cycles or a 3.5% CPU result.
The half-rate parent `86fe3d8` measured 3.558404% median whole-voice C8
benchmark cost and 16,542.27 median timer-3 callback cycles across three runs
on OS 1.2.1. The earlier cheaper revision `392f8e6` measured 4.390% whole voice.
Those are historical measurements, not a hardware timing qualification of this
combined exact-division and display-fix build. Device metadata identifies normal
OS 1.2.1 but does not attest the installed firmware hash.

An offline control-flow analysis of the native callback, including its SDK
adapter, gives a conservative bound of 14,289 Blackfin issue packets per
64-sample block with all features and transitions allowed. With unchanged
pitch and controls, expired click/gain ramps, active Buzz and Unison and zero
Drone after its fade, the bound is 9,896. The half-rate drone parent `86fe3d8`,
analyzed with the same operand and phase bounds, gives 15,030 and 9,887.
Exact control quotients remove 741 full-case packets; compiler layout adds
nine settled packets. This refactoring preserves sound and state. The model includes direct callees
and reviewed loop bounds and checks its optimization certificate with rational
arithmetic.
It does not model cache, pipeline or interrupt timing. These are annotated
packet bounds, not simulated or measured device cycles; the full case remains
above the chosen 15,000 estimated-device-cycle goal. The empirical screening
conversion is 1.85 cycles per packet: the largest ratio from four historical
C8 builds plus a 10% engineering margin, rounded upward. It estimates 26,435
cycles for the full case and 18,308 for the settled case. The previous 1.65
factor underpredicted the half-rate parent's 16,542.27-cycle median with an
estimate of 16,314. This failed held-out prediction remains a failure after
refitting; the conversion is a screening estimate, not independently validated
across algorithm changes or a worst-case timing guarantee. The selected sub-15k
estimated-cycle target is not achieved. Native packets and device cycles are
separate metrics. The display fix changes no audio code.


Unison activation clears 512 Q15 samples directly instead of a byte-at-a-time
structure clear. The chien loop compiles separately, and its two lift additions
saturate nonnegative operands. Rates, arithmetic results and state size are
unchanged by those refactorings. Before the rate reduction, a deterministic
32,768-callback host comparison matched all output
samples and final machine/voice state against `b19c279`, including full-range
pitch/control changes and Unison restarts, under undefined-behavior sanitization.

The drone wave bank interleaves its two Rosin endpoints at each phase point to
share address calculations. Its size remains 37,152 bytes. Half-rate phase steps
double and the regenerated waves retain fewer harmonics than `121ef34`.
All 36 sustained drone tuning probes remain
within eight cents (largest measured error 5.01 cents). Melody tuning remains
within six cents across 81 probes. Existing Unison and scalar solver tests pass.

Pitch and Unison-window division use a 256-entry reciprocal seed table, two
Q31 refinement steps and a one-step remainder correction. Every divisor in
their callback domains is checked exhaustively against unsigned integer
division, including under undefined-behavior sanitization. The 828 detune
quotients are precomputed exactly. Total constant storage is 2,680 bytes, with
no new voice/shared state. A separately compiled 32,768-callback trace matches
all output samples and final state against `86fe3d8` under UBSan.

Only phase reduction still uses the SDK divider; its actual operand ranges
bound it to seven steps. Coup phase can wrap at most once in eight drone samples because its
maximum total increment is below 2^32. These are reviewed source invariants tied
to the native graph, not device cycle measurements.

The string sample kernels compile separately from delay setup. Their arithmetic,
rounding, rates and state transitions are unchanged; the scalar reference test
compares complete delay-line state and samples.
Host tests and native package verification do not establish device timing or
listening quality.

## Animated string outlines

Melody and Unison drawings use narrow closed ribbons with separated forward
and return edges. Their strokes meet across the ribbon, retaining a 1.5-pixel
screen thickness. This avoids the old zero-area retrace and 180-degree endpoint
reversals; every phase has distinct adjacent vertices and fixed nut/bridge
anchors. The display regression covers all 65,536 phases at zero and maximum
amplitude. Device confirmation of the reported endpoint streaking is still
required. This drawing change does not modify audio sources or controls.

## Existing drone ownership limitation

`start` marks `claim_pending`; ownership is assigned when `render` encounters it.
Starting owner 2, then owner 1, but rendering 1 before 2 can give owner 2 the drones
and render them twice during onset. The serial heuristic is not an audio-block ID.
Shared drones and their controls span tracks using this machine. This branch does
not change that ownership design. `benchmarks/audio.cpp` retains its diagnostic.

Host tests do not prove recovery, worst-case device scheduling or listening quality.
