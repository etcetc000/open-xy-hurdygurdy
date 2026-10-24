# Hurdy Gurdy development

This is a standalone Open-XY SDK 1.0 machine. Read README.md and AUDIT.md.
The SDK is a sibling checkout; do not modify it for machine-specific behavior.
Keep controls in their original order and preserve the hurdygurdy name.
Use portable C++11 and public SDK headers. Audio callbacks must not allocate,
block, log, access files or call graphics. Keep tests in tests/.

Run the SDK's `check` and `test --machine <this folder>` after changes.
Native changes also need a firmware build and full release verification using
the SDK's docs/build.md, with all private fixtures required. Host timings do
not establish device cycles, polyphony limits or recovery.

Keep tools and generated non-firmware output in the SDK's ignored local/;
keep all firmware, stock content, preset data and firmware-containing outputs
outside every checkout. No device operations without a separate instruction.
Run commands without a PTY. Never open a terminal window; explicitly launch
background helpers hidden, and use CREATE_NO_WINDOW for Python subprocesses.

Distribution permission is recorded in LICENSE; see PROVENANCE.md for source
attribution. Do not invent or broaden license terms.
