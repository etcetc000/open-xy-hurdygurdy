# SDK 1.0 conversion and performance audit

Audited 2026-10-24 against Open-XY commit
`e8fc2a13bca715079f722df5a23426fac6b8f30b`. Input provenance is in
[PROVENANCE.md](PROVENANCE.md); commit `25cf611` preserves the imported source.

## Assessment

The interface migration is small. This is already a portable, fixed-point
SDK synth with separate audio/display callbacks, bounded delay storage and
eight shared melody leases. A rewrite is unnecessary. Release readiness is
a larger task: shared-drone ownership has a reproducible ordering bug, CPU
needs device qualification, musical behavior lacks reference tests, and the
archive supplies no redistribution license.

The author did consider optimization: Q15 delay storage, only one fractional
delay tap per string sample, four pitch divisions per block, per-block body
coefficients, folded resonator gains, shared drones and bounded melody leases.
The original README reports a host comparison with 2op FM and explicitly says
device CPU was not measured. The archive contains no tests, timing harness,
table generator or measurement records to reproduce those claims.

## Conversion completed

| Area | Change or finding |
| --- | --- |
| Version | Added `"sdk": "1.0"` to machine.json; the untouched input fails here first |
| Display declaration | Removed obsolete `kind`, added `texts: 0` |
| C++ frame | Changed to `Frame<4, 64, 46, 0, 16>` |
| Artwork | Existing machine-prefixed IDs and distinct authored mutable-path points already comply; retained unchanged |
| Controls | Preserved all eight slots, mappings, defaults and twelve definition presets |
| Identity | Preserved requested engine 36/page 84; actual assignments follow each build's layout |
| Packaging | Standalone root machine, pinned SDK CI, source provenance, regression tests; no legacy installer or embedded player |
| Unison CPU | Snap an inaudible residual gain below 256 Q31 units to zero when switched off |
| Display CPU | Geometry-derived string keys; skip rebuilding unchanged string, rim and crank paths; initialize header overrides |

The original unison smoother divides by 256. With a zero target, values
1..255 never decrease, so the twin continues rendering for the rest of the
note. The regression test failed on this condition before the fix. The new
cutoff is below 1.2e-7 of full scale; it does not change active-unison mapping.

The original string keys advance with frame count even under reduced motion.
A probe with Unison on and unchanged controls observed 2,000 path-key changes
in 1,000 reduced-motion frames. The migrated display produces zero after the
initial frame. The normal animated geometry and artwork are retained.

## Remaining findings, in priority order

### 1. Drone ownership and handover (high)

`start` only sets `claim_pending`; `render` claims ownership when it encounters
that flag. Start owner 2, then owner 1, then render 1 followed by 2: owner 2
wins although owner 1 is newest. `drone_serial` increments twice, so the claim
that shared drones render once per block is also false during that onset.
`benchmarks/audio.cpp` reproduces this with `newest=1, actual=2, drone renders=2`.

The drones, controls and cached block are shared across every track using this
machine. Ownership changes therefore affect which track's Drone/Buzz/Tonic
settings drive all drones. The serial heuristic is not a true audio-block ID;
review order changes, owner release, muted/stolen voices and crossfades too.

Implement explicit note-start ordering and an owner election/handover design
within the public callbacks. Test ascending and descending render orders,
multiple simultaneous starts, newest-note release, capacity stealing, parameter
changes and multiple tracks. Establish whether global drones across tracks are
the desired musical behavior before changing that design. This is outstanding;
the migration does not claim to fix it.

### 2. CPU qualification and optimization (high)

SDK host estimates in this session ranged approximately 121k–167k equivalent
Blackfin instructions per voice block, above the SDK's 15k aim/24k warning.
These are calibration-based host estimates, not disassembly instruction counts,
device cycles or evidence of a specific safe polyphony. Host compiler behavior,
load, shared drones and persistent state affect the estimate. The SDK sweep
changes one control at a time and does not establish the combined maximum.

The additional benchmark uses fresh state per repeat, warms up for 1,200
blocks, times 2,000 blocks and reports the best of five repetitions. These
Windows LLVM-MinGW Clang `-O2` timings are nanoseconds per complete group of
voices at 440 Hz, including one shared drone set when enabled:

| Setting | Original, 1 voice | Migrated, 1 voice | Original, 8 voices | Migrated, 8 voices |
| --- | ---: | ---: | ---: | ---: |
| Melody | 2,439 | 2,476 | 20,898 | 21,939 |
| Unison | 3,344 | 3,415 | 32,869 | 30,640 |
| Drones + buzz + coup | 6,527 | 7,306 | 24,763 | 25,418 |
| All features | 8,564 | 8,513 | 37,143 | 38,120 |
| Unison off after on | 3,362 | 2,555 | 32,239 | 23,938 |

The last row improves about 24% for one voice and 26% for eight voices.
Other differences are not claimed as improvements; this is a single before/
after session with host noise. It does not establish native speedup percentages.

Next optimization candidates:

- Cache pitch-to-period results when increments repeat across the four
  segments/blocks; retain correct pitch modulation and note-reset behavior.
- Avoid bowing inaudible bourdon/mouche strings when only Buzz needs the
  trompette. Define how dormant strings resume before changing this behavior.
- Cache body/gain coefficients while controls are unchanged; profile whether
  the state/memory cost is worthwhile.
- Review note-start clearing of a 4.6 KiB melody slot, especially simultaneous
  notes. Steady-state render timing omits that burst.
- Profile native generated code, stack and memory traffic before rewriting
  multiply loops. Existing constant power-of-two division is not equivalent
  to expensive variable division; preserve signed rounding when optimizing.

Measure sustained and retriggered 1/4/8-note loads, all features, heavy stock
effects, parameter locks and multiple tracks on a device in a separately
authorized session. Listen for dropouts and assess GUI responsiveness together.

### 3. Sound and lifecycle qualification (medium/high)

The new audio test covers 128 continuous-control corners, varying tonic,
zero/maximum/modulated pitch, declared output peak, unison shutdown and stolen
lease safety. Both test programs also pass undefined-behavior sanitization.
This is useful coverage, not proof over every state trajectory.

Still needed: all-preset rendered references, pitch measurement over the
claimed range, low-note folding, long-held notes, silence/DC/clipping statistics,
automation discontinuities, repeated note starts, abrupt tonic changes and
drone handovers. Preserve the original recording/presets outside the checkout
as listening references. The loudness-grid and STK-style friction-table
derivations are absent; document their provenance and generation if available.

### 4. Display qualification (medium)

Four paths of 64 points and 46 nodes fit the SDK limits. Tests cover 1,000
animated frames with control variation, negative/late elapsed time, track
switches, note snapshots, bounds and geometry-key coherence. Reduced motion
now avoids redundant path invalidation. Native rasterization cost remains
unmeasured. The two straight-segment melody shapes contain many collinear and
retraced points; reducing geometry may help, but the shared 64-point frame
budget and rim path require coordinated C++/SVG changes and visual comparison.

### 5. Release and distribution (blocking for publication)

No license or author notice was supplied. Confirm ownership/redistribution
terms, including any STK-derived content, before public release/catalog entry.
The repository is private for audit and development; it is not a release.

Retain `hurdygurdy` and all parameter slot meanings. Presets identify the name;
projects identify numeric engine assignments. Importing an old hardcoded-ID
project needs the original firmware's layout preserved, not simply the new
build's preferred engine/page fields. The old bundled Mac installer is not a
current distribution mechanism; use the SDK's build/catalog process.

## Estimated remaining scope

Planning estimates, assuming SDK familiarity and access to the original author:

| Work | Engineering time |
| --- | --- |
| Interface migration, repo, basic tests and small fixes | Completed in this audit |
| Drone ownership design, implementation and regressions | 1–2 days |
| Sound/preset/tuning reference tests | 1–2 days |
| CPU profiling and selected optimizations | 1–3 days, depending on measured bottleneck |
| Display review and device stress session | 0.5–1 day plus device access |
| Licensing and catalog release | External dependency; no reliable elapsed-time estimate |

## Validation record

- SDK public `python sdk.py check`: passed, 143 Python tests in each normal
  and `-O` run; public mode explicitly skips 17 private-fixture tests.
- Pinned-SDK `sdk.py test --machine ...`: preview ABI/lifecycle/drawing smoke
  checks and both machine tests passed. CPU warning remains.
- Clang undefined-behavior sanitizer: audio and display tests passed.
- Native build and full private-fixture verification: recorded below when complete.
- No device operations, hardware timing, recovery test or listening approval.

The active SDK checkout was edited concurrently by another task during an
initial build; its source-integrity check rejected that build. Native
qualification uses a separate checkout at the pinned commit, with unchanged
stock/tool authentication and fixtures. All firmware outputs are external to
every checkout under `%LOCALAPPDATA%/OPXY-SDK/private/builds/`.
