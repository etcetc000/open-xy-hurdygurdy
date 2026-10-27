# Hurdy Gurdy

A standalone Open-XY SDK 1.3 machine based on Hurdy Gurdy by
[SammiBabe666](https://github.com/SammiBabe666), the original instrument author.
See [AUDIT.md](AUDIT.md) for scope, findings and validation, and
[PROVENANCE.md](PROVENANCE.md) for source attribution and [LICENSE](LICENSE)
for the recorded distribution permission.
Validation uses SDK commit `09e5b94faf21751e1357e2311e2afa82fddd4275` (OS 1.2.1).

A bowed-string synth. Each note bows a melody string (the chanterelle) with a
friction model of a rosined wheel. Three drone strings (bourdon, mouche and
trompette) are bowed once for the whole machine by the newest note, so chords
never stack drones and the drones carry on unbroken from note to note. The
trompette rattles the chien, the buzzing bridge, when the wheel is stroked.
Everything passes through a three-mode soundbox.

| Encoder | Turn | Press, then turn |
| --- | --- | --- |
| Blue | **Wheel**: bow speed; faster is brighter and stronger | **Rosin**: grip; low is airy, high is gritty |
| Green | **Buzz**: chien sensitivity; above half it buzzes all the time | **Coup**: wrist strokes; up to 40% one stroke on each note, above that repeating strokes from 1.5 to 9 Hz, restarted by each note |
| White | **Body**: soundbox from small and bright to large and warm | **Drone**: drone level, 0 is off |
| Orange | **Unison**: a detuned copy of the chanterelle; joins by 12%, then detunes up to 22 cents | **Tonic**: the drone key (C to B) |

Each encoder toggles between two parameters when pressed. Tonic remains in
saved parameter slot 7; moving its encoder preserves preset and project values.

The drones are tuned in just intonation from the tonic: bourdon in G1 to F#2,
mouche a twelfth above (3:1), trompette two octaves above (4:1). They sound
while any note on the machine sounds; for a hands-free drone, use the OP-XY's
Hold player and play one note.

The screen is a grey top view of the instrument: the crank and wheel rim turn
with Wheel, rosin dust follows Rosin, the chien jumps with Buzz and Coup, the
soundbox grows with Body, the drone strings brighten with Drone, the key
letter shows Tonic and a second melody string appears with Unison.

```sh
python ../open-xy/sdk.py preview --machine . --out ../open-xy/local/hg-preview-001
python ../open-xy/tools/serve_harness.py --preview ../open-xy/local/hg-preview-001
python ../open-xy/sdk.py test --machine . --host-cxx g++
python ../open-xy/sdk.py check
```

| File | Owns |
| --- | --- |
| [definition.json](definition.json) | Controls, presets, resources and the drawing shape |
| [core.h](core.h), [engine.cpp](engine.cpp) | String, drone and voice state; mixing, chien, soundbox |
| [bowed_string.h](bowed_string.h), [strings.cpp](strings.cpp) | Shared bowed-string solver with settled-delay and moving-delay paths |
| [tables.h](tables.h) | Friction curve, drone periods, soundbox modes, coup rates, unison ratios, loudness grid, display sine |
| [display.h](display.h), [display.cpp](display.cpp), [panel.svg.fragment](panel.svg.fragment) | The screen |
| [machine.json](machine.json) | SDK version, kind, requested identity (engine 36, page 84) and icon |

## Sound model

Each string is a digital waveguide split at the wheel: a neck line (wheel to
nut) and a bridge line (wheel to bridge), with the wheel at 11% of the string.
The bridge reflection passes a symmetric three-tap loss filter with power-of-two
coefficients: 31/32 DC gain and quantized curvature, implemented with shifts. The bridge
side holds whole samples and the neck side the fraction, so each string
sample needs one interpolated tap. The melody runs at 22.05 kHz; the
three drones, Unison copy and soundbox run at 11.025 kHz. A half-band filter
decimates the melody before mixing; the final signal is interpolated to 44.1 kHz.
Unison uses two crossfaded fractional delay taps, one melody period apart,
to shift the main string by up to 22 cents. It shares the main string's bow
noise and harmonic evolution instead of simulating another independently bowed
string. The copy has a short, pitch-dependent delay; attacks and beating differ
from the physical twin. Its history resets when Unison is re-enabled.
The main string and all three drones retain their physical feedback and
nonlinear bow interaction. Fractional delay interpolation is retained for tuning.
The reduced rates change brightness and intonation: the tuning regression
allows eight cents across 81 sustained melody probes from 110 to 1,760 Hz.
Friction follows the STK bowed-string curve, clip((u + 0.75)^-4, 0.01, 0.98),
in a 1,025-entry table with nearest-entry lookup. Wheel sets bow speed and string brightness, Rosin the
curve's slope and the wheel's roughness noise. A measured Wheel × Rosin
loudness grid sets the output gain, so those two knobs mostly change character.

The chien takes impacts from the peak of each trompette swing, roughened and
rung through a 2.2 kHz resonator, and is lifted by Buzz and by each coup.
Notes below about 44 Hz fold up an octave.

## Native notes

- 32-bit arithmetic only: Q15 audio/filter state, Q31 control envelopes, Q12 delay
  lengths, pitch-to-period division only when the sampled pitch changes.
  No 64-bit helpers. Unison gain uses unsigned constant division to avoid a
  software division helper inside the sample loop.
- Delay lines store Q15 words. A melody slot (chanterelle and detuned-copy history) is about 2.19 KiB;
  eight slots are shared by every track through `LeasePool`, so at most eight
  melody notes sound at once and a ninth takes the oldest note's strings. That
  also bounds the engine's worst-case CPU.
- Memory: 21,792 bytes shared plus 92 bytes per voice (128 voices).
- Internal mixing stays in Q15 until the public Q31 output boundary. The
  half-band decimator uses four history values held in registers. The string
  solver is compiled once in `strings.cpp`, with a constant-fraction path for
  settled pitch. The shift-based loss filter intentionally changes damping and rounding.
- Per sample, a sounding note costs one physical melody string and the
  soundbox; Unison adds a feed-forward delay resampler. The drones cost three strings once for the whole machine, only
  while Drone or Buzz is above zero. With only Buzz audible, bourdon and
  mouche pause after their level fades to zero, then resume when Drone rises.
  The chien runs only while Buzz is up. A voice whose strings were taken
  and that holds no drones does no work.
- Output is limited below 0.3, the declared peak.
- Device CPU must be measured for this revision with the SDK profiling build.
  The source base (PR #147, `b1366c2`) measured 4.741% median whole-voice
  benchmark cost on OS 1.2.1. That is a baseline, not this revision's result.
  Drone ownership still needs correction before release.

## Development

Requires the SDK preview prerequisites (Python, NumPy, Node, Clang with
wasm32 support) and a host C++11 compiler. On Windows, select a Clang build
with wasm32 support using `--cxx`; some LLVM-MinGW builds only target Windows.

`tests/audio.cpp` checks control corners, pitch extremes, output bounds,
unison shutdown and voice stealing. `tests/presets.cpp` exercises all twelve
presets with eight voices, different controls per voice, automation and
retriggers. The interpolation and friction tests check numeric accuracy;
`tests/display.cpp` checks frame bounds, initial note snapshots, reduced
motion and geometry keys. Drone idle/resume and sustained tuning have
additional regression tests. `tests/decimation.cpp` compares the optimized
filter with a full seven-tap FIR; `tests/string_kernel.cpp` compares settled
and moving delays with an independent scalar specification of the quantized
loss filter, including index wrapping. `tests/detuned_copy.cpp` checks the
copy's frequency over 18 pitch/detune combinations; `tests/detuned_transitions.cpp`
checks storage bounds, full-range automation, index wrap, constant gain and
silence drain.
CI also runs these with undefined-behavior
sanitization and compiles the native module.

`python generators/friction.py` regenerates the friction curve from
clip((u + 0.75)^-4, 0.01, 0.98). The nearest-entry approximation changes the
sound slightly; it avoids interpolation in every string sample. Body and
level coefficients are cached until their controls change.
`python generators/rates.py` regenerates the body/chien poles and envelope
decays at quarter rate from the original coefficients in
`generators/filter_reference.json`.

`benchmarks/audio.cpp` times one and eight voices with melody, unison, drones,
all features, and unison switched off. Compile with `-std=c++11 -O2`,
`-I../open-xy/sdk/include -I.`, placing the executable in the SDK local folder.
It also reproduces the outstanding drone-owner ordering issue. Host timing
is diagnostic and has no pass/fail performance threshold.
`benchmarks/corners.cpp` extends this to 2,816 settings: continuous-control
corners, eight pitch inputs including extremes, segment pitch modulation,
and every tonic in four heavy combinations. It reports the best of seven
2,048-block timings after warmup for each setting.

The requested engine/page numbers are hints for a new build. Preserve the
previous package layout when rebuilding existing projects; an older image
that hardcoded engine 36 needs its original layout reconstructed first.
