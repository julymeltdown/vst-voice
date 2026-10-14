# Saved, tuned lyric song through the loaded CLAP plugin

2026-10-15, Asia/Seoul. **Local engineering capability verified; Beta BLOCKED.**
Fetch and fast-forward pull ran first; integrating base is
`ccbb39d64b03bb2c685b801e8f50bf0af5db3d9b`. This extends the
[saved-project host lane](INSTALLED_CLAP_PROJECT_2026-10-15.md) beyond its
one-note fixture. It reuses the existing original-song journey rather than
creating a second installer, song or tuning implementation.

## Defects and repair

The existing native case, `Tuning an installed singer survives undo, save,
reopen and export`, authors 48 Japanese lyric notes over 41 seconds at 120 BPM.
It includes consonants, unequal note lengths, a score rest, sustained final
vowel and a drawn two-point formant curve. Its tuning, cancellation/retry,
undo/redo, save/reopen and audio-export assertions already pass. The case now
emits the saved project path after those assertions; CMake supplies that runner
to the loaded-host Python test. The Python test removes only that invocation's
temporary producer directory and signed package before the cold host load.
The installed resource remains available by its recorded identity.

Two separate problems prevented the loaded-plugin journey:

1. The internal host bounded scores to 30 seconds and FollowHost capture to
   64 beats. The rebuilt baseline refuses the saved 41-second project before
   module loading or output. Against retained baseline host/plugin binaries,
   five Python methods run: the four existing methods pass and the new song
   method fails on the 30-second refusal. The host now permits an end time of
   65 seconds including placement/rest and at most 128 captured host beats,
   retaining the 256-note/16-MiB limits. This covers this 120-BPM fixture;
   it is not unrestricted support for every possible 60-second tempo map.
   Admission also checks room for all probe-added tick placement before
   addition, with an extreme valid-original-region-end rejection trial.
2. With only those host limits repaired, the plugin loads and round-trips the
   project but refuses Final and produces zero frames. In
   `libs/seam-clap-editor/src/editor_runtime_project.cpp`,
   `makeOfflineIdentity` encodes with `includeRendererProvenance=false`, while
   the publication's source-digest check used the default `true`. The saved
   song carries nonempty render ABI/compiler provenance from its earlier
   export. Comparing these two encodings rejects the same musical source.
   The publication check now uses the same option. Other project content,
   revision, source, format, currency and incomplete-Final guards remain.
   The same saved input and installed files subsequently pass the loaded
   plugin's score, format, Final, transport and overflow controls. The code
   mismatch and isolated repair establish the cause; the public host did not
   expose the runtime's internal diagnostic string.

## Executed checks and retained audio

The recorded full untargeted Release rebuild after the library repair passed in 4.369s.
**Review correction:** the committed runtime source was written after this build
and its CTest, so these historical logs do not bind the complete committed bytes
to the tested plugin. The [later review supplement](INSTALLED_CLAP_SONG_REVIEW_2026-10-15.md)
adds a source-bound rebuild, native provenance regression and direct song run;
it does not authenticate historical execution.
Eight focused CTest entries passed in 33.337s: five Python methods, 73 native
cases (5/12/3/12/7/34), and the phase-12b executable. The latter is one program,
not a newly counted native case. Native child calls in the Python methods
reuse existing cases. The original song case count remains seven; this unit
adds one Python method and one admission subtrial. On macOS the admission
method now has 14 trials. The prior 30.211s failing CTest run is retained:
the host method fails while both native journey suites pass.

The separate direct host run passes in 1.470s after producer/package deletion.
It uses an actual dynamically loaded plugin and public CLAP state/render/
activation/process APIs. All 48 score notes are checked at both 44.1 and
48 kHz: 96 note windows and 3,845,175 frames summed across rates. No note events
are sent. The retained 48-kHz float32 stereo WAV has **2,004,000 frames**,
41.75 seconds including 0.5-second inserted rest and 0.25-second tail,
3,849,312 nonzero words, peak 0.1054771468 and sum of squares 948.1579043.
Raw decoding verifies finite signal and derives frame extents from the saved
score. Only 48-kHz PCM is retained; neither combined raw score energy nor
FollowHost energy can be independently reconstructed from that one WAV.
FollowHost reports a 2.75-second entrance and passes its stale/map and
in-block transport refusal controls. These deliberately shifted/timed probes
are not byte-identical playback of the untouched saved project.

## Evidence and remaining acceptance

The [index](INSTALLED_CLAP_SONG_2026-10-15.evidence.json) pins 79 artifacts and
10 preserved inputs. Retention includes baseline/final source and binaries,
generated header, failing and passing receipts/logs, the saved project,
installation provenance and raw PCM. The initial baseline native invocation
overlapped an unchanged full build; subsequent regression/direct host runs
occurred after completed rebuilds. No authenticated clean-build or historical
execution lineage is asserted. Two initial evidence-setup commands failed
because lyrics live in a region table, not each note; they produced no native
or host result and are not counted as product failures.

The read-only verifier hashes the exact set and original inputs, checks Git
source when requested, receipts/counts, original-project identity, source/
package deletion, finite PCM and frame calculations. Absolute installation
paths remain required; copied provenance is not a relocated replay package.
The native fixture recorded signature/trust verification; its package is now
deleted, so this audit performs no fresh package-signature verification.
No private keys are retained. Review of the preceding `ccbb39d6` unit remains
active in **Review in English**; this unit's independent review has not yet
completed and must follow that review without interrupting it.

A separate retained source audit again verifies the 297 existing internet WAVs:
25 clean VocalSet f1 recordings (265.178821s) and 272 Karoryfer female growls
(338.473401s). It downloads no additional audio or changes any original.
The [source dossier](FEMALE_INTERNET_SOURCE_REFRESH_2026-10-15.md) binds original
publisher licenses. Genuine producer/distinct reviewer, source/performer and
public-license disposition, source-to-target phonetics, coverage, failed-QC
policy and listening remain unresolved. The clean singer and growls remain
separate. None is imported or qualified by this song unit.

This is an installed legacy signed procedural test resource and a full-song
engineering regression. Human Gate A/Gate B, acoustic F0/phase/quality,
released classical/neural resources, late-publication/currency work, canonical
tolerance/integration, physical-device/DAW/Windows and release evidence remain.
Alpha is **0/20 recorded passes**, Beta **BLOCKED**, U45 held, and the original
goal **ACTIVE**. Canonical files, recordings, stash, `.github` and README's
Windows TODO remain unchanged.


## Independent review update

**Review in English** approves exact `fe340737c0709a45f1d48cdfec8ec0089ea814ce`
for engineering scope, with a P2 historical lineage finding and P3 native regression
follow-up. The full 17,208-byte verdict is retained by the [review supplement](INSTALLED_CLAP_SONG_REVIEW_2026-10-15.md).
No code change to the repair was required. Later supplementary evidence and its new
native test need their own review; approval does not extend to later publication,
pitch or host-admission units, rights/listening, canonical gates or Beta.
