# Hurdy Gurdy implementation notes

This branch starts from PR #147's `b1366c21a9ccbc0221ba4d326461b0c9c5904bdb`.
Original authorship and distribution permission remain in PROVENANCE.md and LICENSE.
The public machine name, parameter order, encoder pairs and twelve presets are unchanged.

## Sound and cost

The main melody string retains the physical bowed-string solver at 22.05 kHz.
The three shared drones retain their physical solvers at 11.025 kHz.
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
lower-cost implementation. Shared state is 21,792 bytes, with 92 bytes per voice.

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
reference specifies the new filter's signed rounding independently.

The SDK build must authenticate stock/tool inputs, reconstruct the package and
pass full native release verification. Firmware and captures stay outside every
checkout. Host timing does not establish device cycles or a 3.5% CPU result.
The preceding cheaper revision `392f8e6` measured 4.390% median whole-voice
benchmark cost on OS 1.2.1. This revision has no hardware timing qualification.

An offline control-flow analysis of the native callback, including its SDK
adapter, gives a conservative bound of 22,416 Blackfin issue packets per
64-sample block with all features and transitions allowed. With unchanged
pitch and controls, expired click/gain ramps, active Buzz and Unison and zero
Drone after its fade, the bound is 12,789. The preceding revision's corresponding
bounds were 24,636 and 14,384. The model includes direct callees and reviewed
loop bounds and checks its optimization certificate with rational arithmetic.
It does not model cache, pipeline or interrupt timing. These are annotated
packet bounds, not simulated or measured device cycles; the full case remains
above the 15,000-packet goal. Host tests and native package verification do not
establish that timing goal or listening quality.

## Existing drone ownership limitation

`start` marks `claim_pending`; ownership is assigned when `render` encounters it.
Starting owner 2, then owner 1, but rendering 1 before 2 can give owner 2 the drones
and render them twice during onset. The serial heuristic is not an audio-block ID.
Shared drones and their controls span tracks using this machine. This branch does
not change that ownership design. `benchmarks/audio.cpp` retains its diagnostic.

Host tests do not prove recovery, worst-case device scheduling or listening quality.
