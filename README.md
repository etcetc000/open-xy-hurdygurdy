# Hurdy Gurdy

A standalone Open-XY SDK 1.0 migration of the supplied Hurdy Gurdy source.
See [AUDIT.md](AUDIT.md) for scope, findings and validation, and
[PROVENANCE.md](PROVENANCE.md) for source and unresolved licensing.
CI pins SDK commit `e8fc2a13bca715079f722df5a23426fac6b8f30b`.

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
| Orange | **Unison**: a second chanterelle; joins by 12%, then detunes up to 22 cents | **Tonic**: the drone key (C to B) |

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
| [core.h](core.h), [engine.cpp](engine.cpp) | String, drone and voice state; bowing, chien, soundbox |
| [tables.h](tables.h) | Friction curve, drone periods, soundbox modes, coup rates, unison ratios, loudness grid, display sine |
| [display.h](display.h), [display.cpp](display.cpp), [panel.svg.fragment](panel.svg.fragment) | The screen |
| [machine.json](machine.json) | SDK version, kind, requested identity (engine 36, page 84) and icon |

## Sound model

Each string is a digital waveguide split at the wheel: a neck line (wheel to
nut) and a bridge line (wheel to bridge), with the wheel at 11% of the string.
The bridge reflection passes a symmetric three-tap loss filter (exactly one
sample of delay), so a note's loop delay is the period minus one sample and
tuning stays within a cent up to C6. The bridge side holds whole samples and
the neck side the fraction, so each sample needs one interpolated tap.
Friction follows the STK bowed-string curve, clip((u + 0.75)^-4, 0.01, 0.98),
in a 257-entry table. Wheel sets bow speed and string brightness, Rosin the
curve's slope and the wheel's roughness noise. A measured Wheel × Rosin
loudness grid sets the output gain, so those two knobs mostly change character.

The chien takes impacts from the peak of each trompette swing, roughened and
rung through a 2.2 kHz resonator, and is lifted by Buzz and by each coup.
Notes below about 44 Hz fold up an octave.

## Native notes

- 32-bit arithmetic only: Q31 signals through `opxy::q31::multiply`, Q12 delay
  lengths, one 32-bit division per 16 samples for pitch. No 64-bit helpers.
- Delay lines store Q15 words. A melody slot (chanterelle and twin) is 4.6 KiB;
  eight slots are shared by every track through `LeasePool`, so at most eight
  melody notes sound at once and a ninth takes the oldest note's strings. That
  also bounds the engine's worst-case CPU.
- Memory: 44,620 bytes shared plus 80 bytes per voice (128 voices).
- Per sample, a sounding note costs one string (two with Unison) and the
  soundbox. The drones cost three strings once for the whole machine, only
  while Drone or Buzz is above zero; the chien runs only while Buzz is up. A
  voice whose strings were taken and that holds no drones does no work.
- Output peaks near 0.22 in normal use and is limited at 0.3, the declared
  peak.
- Device CPU has not been measured. The current SDK host estimate exceeds
  its CPU guidance; see [the audit](AUDIT.md) for measured scenarios and limits.
  Drone ownership still needs correction before release.

## Development

Requires the SDK preview prerequisites (Python, NumPy, Node, Clang with
wasm32 support) and a host C++11 compiler. On Windows, select a Clang build
with wasm32 support using `--cxx`; some LLVM-MinGW builds only target Windows.

`tests/audio.cpp` checks control corners, pitch extremes, output bounds,
unison shutdown and voice stealing. `tests/display.cpp` checks frame bounds,
initial note snapshots, reduced motion and geometry keys. CI also runs these
with undefined-behavior sanitization and compiles the native module.

`benchmarks/audio.cpp` times one and eight voices with melody, unison, drones,
all features, and unison switched off. Compile with `-std=c++11 -O2`,
`-I../open-xy/sdk/include -I.`, placing the executable in the SDK local folder.
It also reproduces the outstanding drone-owner ordering issue. Host timing
is diagnostic and has no pass/fail performance threshold.

The requested engine/page numbers are hints for a new build. Preserve the
previous package layout when rebuilding existing projects; an older image
that hardcoded engine 36 needs its original layout reconstructed first.
