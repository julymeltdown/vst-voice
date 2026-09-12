# U35 canonical Phase 12C evidence boundary

`scripts/verify_phase12c_canonical_contract.py` separates engineering harness
output from canonical release evidence.

Source mode (`--root`) verifies that the CLAP target is the production
`com.project-seam.editor`, that it links the `seam-live-voice` engine, that the
32-voice/1,024-event/256 MiB limits and activation-time scratch buffer remain
declared, that CLAP and MIDI dialects are advertised, and that the canonical
path contains no legacy `LiveSampleInstrument` or generated human fixture
symbols. The validator script also has to name the pinned clap-validator
0.4.1 tool.

Strict artifact mode additionally requires a non-linked plugin and voicebank,
rehashes both, and checks every supplied matrix/validator/soak record against
those identities. The matrix must report exactly 336 finite, zero-failure
cases; validator output must be a PASS from 0.4.1 with a bound raw-log hash;
and a full soak must carry source/build/plugin/bank identity, positive runtime
counters, zero event overflow, no more than 32 active voices, and at least
7,200 seconds when `--require-full-soak` is used.

The matrix and soak runners now consume the canonical CLAP bundle path and
production-bank root when invoked by CTest (fixture-only mode remains
available when those arguments are omitted). They rehash the bundle/bank,
load the bank through `VoicebankCatalog` and `buildTrustedResource`, and emit
the bound identity fields. Release CTest and strict artifact verification pass
for the 336-case matrix and the five-second smoke soak. The pinned official
validator, target-platform/DAW runs, installed-byte evidence and exact
7,200-second soak remain open; smoke is not a release substitute.
