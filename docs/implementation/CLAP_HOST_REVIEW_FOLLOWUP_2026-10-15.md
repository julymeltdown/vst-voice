# Pinned CLAP host admission follow-up

Date: 2026-10-15, Asia/Seoul. Integrating base: `d9860c7e90d6ccd7af284bdf59cc26c12c4046f0`.
Fetch and fast-forward pull completed first; the checkout was clean and current.
**Locally verified engineering repair; current unit review pending, Beta BLOCKED.**

## Problem and implementation

The independent **Review in English** verdict approves the exact earlier
`ccbb39d64b03bb2c685b801e8f50bf0af5db3d9b` installed-project unit with no required
changes and three P3 follow-ups. The retained full verdict is 19,109 bytes,
SHA-256 `df5af2c0c65174cbe63cde8e38d6324c537410ff51e8844bd6efb3d0d7fdef3f`.
It is not an approval of this later change. The saved full-song unit `fe340737`
is under a separate live review; later publication and pitch units remain unreviewed.

The [internal host](../../apps/seam-clap-editor-host/main.cpp) previously treated
empty project/digest values as an absent project and accepted duplicate options
by retaining the last value. Its parser now refuses duplicate options, empty or
missing values, and values beginning with `--` before loading a module. A file
whose name begins with `--` can be supplied using a path such as `./--name`.
The behavior applies to both existing host modes. Existing valid controls remain.

`withinOfflineProbeBudget` previously bounded notes, Tick padding and fixed-audio
seconds but did not enforce the FollowHost probe's captured-beat limit. Admission
now requires at least one note on an unmuted track and bounds every unmuted region
end by `(128 - 3) * ppq - 960` ticks. The probe adds two host-placement beats,
one region beat and a 960-tick leading rest. At PPQ 960, a region ending at beat
124 reaches the exact 128-beat bound; beat 125 is refused even though it fits the
65-second fixed-audio limit at 120 BPM. The original 256-note, 65-second and
overflow checks remain. This is an internal probe budget, not a product song limit.

The caller-pinned route previously protected only the project itself from output
aliases. It now requires audio and summary destinations to be new and distinct.
`symlink_status` refuses existing files, directories, hard links and dangling or
ordinary symlinks; canonical comparison rejects two names for the same new output.
Existing direct/canonical/hard-link project-alias diagnostics take precedence.
The default host capture lane retains its existing output behavior.

This check runs before module loading and writing outputs. It is **admission-time
protection**, not exclusive file creation or protection from concurrent filesystem
replacement. It does not forbid creating a new file inside an installed resource
directory or enumerate the complete resolved bank/neural/media dependency closure.
Offline screenshots are not created by this lane. No broader guarantee is claimed.

## Execution evidence

The [regressions](../../tests/test_clap_offline_host.py) run against copied immutable
baseline host/plugin/fixture binaries. Three new methods produce **15 failures**
in the corrected baseline, then all three pass with the repaired host. They cover
eight malformed argument sets, the exact FollowHost boundary and two refusals,
five existing/link destination cases, and identical audio/summary destinations.
The boundary positive control reaches intentionally missing module loading; it
does not demonstrate a successful 128-beat audio bounce.

The initial muted-only fixture was invalid because its note extended beyond its
one-beat region. That decode failure and log are retained. The corrected four-beat
fixture reaches baseline module admission and is refused by the repaired budget.
The initial test-source copy is explicitly reconstructed by the documented one-line
replacement, not represented as a contemporaneous source capture.

A retained direct comparison uses only a private owned destination: the baseline
loads the real plugin and replaces **49 bytes with a 672,044-byte float32 WAV**,
writing its summary. The repaired host returns failure before loading the module,
preserves the exact 49 bytes and writes no summary. Neither comparison targets an
original recording or installed recipe. The unchanged project/recipe copies bind
the controlled test input; their installed-resource paths remain temporary dependencies.

The full untargeted Release build passes in **25.673s**. Four actual CTest entries
pass in **31.097s**: eight Python host methods, seven original-singer song journey
cases, 34 procedural installation cases, and the CLAP render-player smoke.
Three separately run regressions are a subset of those eight methods. Native
fixture invocations inside Python repeat existing cases and are not extra cases.
The regex also named a nonexistent editor smoke entry; it ran no such test.
No GUI, physical device, external DAW, Windows, listening or released-bank claim follows.

## Retention and remaining work

The [evidence index](CLAP_HOST_REVIEW_FOLLOWUP_2026-10-15.evidence.json) binds
**55 retained artifacts and eight preserved inputs** under
`out/evidence/clap-host-review-followup-2026-10-15`. The 10,678-byte manifest has
SHA-256 `207213f01b66adff8e00aea03302888c3e80c709d421e440eff419859369f2fd`.
The read-only verifier rehashes those bytes and 75 frozen earlier host artifacts,
checks receipts and direct before/after bytes, and compares source with Git.
It executes neither the plugin nor retained runtime scripts. Binaries, source and
the generated header embedding the integrating base are observations, not authenticated
clean-build lineage. Historical roots are unchanged; their original temporary-path
requirements are not silently repaired or promoted to self-contained replay.

The three earlier P3 follow-ups now stand as follows: admission budget gap addressed
here (Tick padding had already been repaired in `fe340737`); fixture-directory
cleanup still pending; historical verifier temporary-input dependence still pending.
The warmed-instance negative-control wording in the earlier dossier is corrected.
Exclusive output publication, resource-directory output admission, canonical-state
digest reporting and semantic request-input/timeline integration remain separate work.

The 297 downloaded female candidate WAVs remain unqualified: original source/license
provenance, performer/identity/public-license disposition, genuine producer and distinct
reviewer, useful phonetic coverage, failed-QC policy and listening still prevent a released
bank. No new sample is downloaded or imported by this host unit. Full classical/neural
resources and R9 learned-singer evidence, acoustic criteria/tolerance ownership,
user/DAW/platform/release acceptance and independent integration remain required.
Alpha has **0/20 recorded passes**, Beta is **BLOCKED**, U45 remains held and the full
goal remains ACTIVE. `.github`, README Windows TODO, canonical gates, original audio
and the existing stash are preserved.
