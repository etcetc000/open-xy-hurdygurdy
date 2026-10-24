# SDK 1.0 conversion and performance audit

Audited 2026-10-24 against Open-XY commit
`e8fc2a13bca715079f722df5a23426fac6b8f30b`. Input provenance is in
[PROVENANCE.md](PROVENANCE.md); commit `25cf611` preserves the imported source.

## Assessment

The interface migration is small. This is already a portable, fixed-point
SDK synth with separate audio/display callbacks, bounded delay storage and
eight shared melody leases. A rewrite is unnecessary. Release readiness is
a larger task: shared-drone ownership has a reproducible ordering bug, CPU
needs device qualification and musical behavior still needs listening qualification.
The repository owner has confirmed permission for public source publication
and catalog distribution; see [LICENSE](LICENSE).

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

### 2. CPU qualification and optimization

The current implementation retains both travelling-wave delay lines and the
nonlinear bow for every string. Melody and Unison run at 22.05 kHz; the three
drones, chien and body run at 11.025 kHz. A seven-tap half-band filter precedes
melody decimation, and the final output is interpolated to 44.1 kHz. Resonator
poles and envelope decays are retuned by `generators/rates.py`, using the
original coefficients preserved in `generators/filter_reference.json`.

Audio, resonator state and gain mixing use Q15 with bounded 32-bit products;
control envelopes retain Q31 precision. Unchanged body/level coefficients and
pitch conversions are cached. The key-click envelope is evaluated once per
block and interpolated. When Drone fades to zero, bourdon and mouche pause;
trompette continues to excite Buzz. All eight controls and three body modes
remain. These changes intentionally alter the sound and bandwidth.

Current host results, with the same SDK reference loop and Clang `-O2`:

| Probe | Highest sampled instruction equivalents per 64-sample block |
| --- | ---: |
| Ordinary SDK one-control-at-a-time sweep | about 12,400 |
| Expanded 2,816-case sustained/render sweep | 18,728 |
| 384 repeated fresh-note cases, including initialization, render and destruction | 19,192 |

`benchmarks/corners.cpp` supplies the portable expanded sweep: all 128
continuous-control corners, eight pitch inputs including numeric extremes,
steady and changing segment pitches, and all twelve tonics in four heavy
combinations. Each setting uses seven 2,048-block measurements after 256
warmup blocks, with fresh state per repetition. The smallest time per setting
is used, then the largest across settings. These are sampled cases, not an
exhaustive worst-case proof over all continuous controls and histories.

Wall-time measurements were affected by host scheduling, so the expanded and
onset results also used a temporary SDK-local Windows clock adapter calling
[QueryThreadCycleTime](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).
No Windows code enters the machine. It counts the host thread's CPU cycles;
they are not Blackfin cycles. Divide by the same reference's host cycles and
multiply by its 590 Blackfin instructions. Using the smaller calibration
before/after each sweep gives the conservative figures above: render peak
5,499.8 cycles / 173.266 * 590, onset peak 5,642.3 / 173.453 * 590.
No conversion from these counters to elapsed seconds is assumed.

The tested host estimates meet the requested sub-20k target. Device timing,
stock effects, display contention and real multi-track scheduling still need
qualification. No device operations were performed. The separate drone-owner
ordering bug above remains unresolved.

### 3. Sound and lifecycle qualification (medium/high)

The new audio test covers 128 continuous-control corners, varying tonic,
zero/maximum/modulated pitch, declared output peak, unison shutdown and stolen
lease safety. Seven test programs pass undefined-behavior sanitization.
Preset tests additionally exercise eight voices, mixed controls, retriggers
and automation across all twelve presets; friction and interpolation have
separate numeric checks. The sustained-tuning test checks 81 combinations
of nine notes (110-1,760 Hz), three Wheel values and three Rosin values. Its
acceptance bound is eight cents; the largest measured deviation is six cents.
Drone idle/resume tests cover silence, fading and restarting while Buzz stays on.
This is useful coverage, not proof over every state trajectory.

Still needed: listening review of all presets, pitch measurement over the
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

### 5. Release and distribution

The original instrument author is [SammiBabe666](https://github.com/SammiBabe666).
No license or author notice was supplied in the original archive. On 2026-10-24,
the repository owner confirmed permission to publish the source and distribute
the machine through the Open-XY catalog. That permission is recorded in
[LICENSE](LICENSE); it does not assign a general-purpose open-source license.
The original archive's missing table provenance remains documented above.

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
| Catalog release | Publication permission confirmed; release pinned in the catalog submission |

## Initial migration validation record

The following package results describe the initial SDK 1.0 migration. Current
optimization coverage and timing are recorded above; each generated package
requires a fresh native build and full release verification.

- SDK public `python sdk.py check`: passed, 143 Python tests in each normal
  and `-O` run; public mode explicitly skips 17 private-fixture tests.
- Pinned-SDK `sdk.py test --machine ...`: preview ABI/lifecycle/drawing smoke
  checks and both original machine tests passed.
- Clang undefined-behavior sanitizer: audio and display tests passed.
- Display equivalence probe: identical node/path-geometry checksum across
  1,000 varied frames before/after; parameter and preset definitions unchanged.
- Native build: passed, including independent reconstruction, exact edits and
  entry preservation. The full bundled collection plus Hurdy Gurdy allocates
  577,888 bytes of the shared module region. Layout assigns Hurdy Gurdy 36/84.
- Full private-fixture verification: passed. Independent native source rebuild
  matched; all 143 Python tests passed in both normal Python and `python -O`
  with no skipped fixtures, and the browser test suite passed.
- GitHub Actions run [40123051493](https://github.com/etcetc000/open-xy-hurdygurdy/actions/runs/40123051493):
  neither job started; GitHub reports failed account payments or a spending
  limit requiring adjustment. This is an account block, not a test failure.
  Hosted CI remains unverified until that is resolved and the workflow rerun.
- No device operations, hardware timing, recovery test or listening approval.

The active SDK checkout was edited concurrently by another task during an
initial build; its source-integrity check rejected that build. Native
qualification uses a separate checkout at the pinned commit, with unchanged
stock/tool authentication and fixtures. All firmware outputs are external to
every checkout under `%LOCALAPPDATA%/OPXY-SDK/private/builds/`.
