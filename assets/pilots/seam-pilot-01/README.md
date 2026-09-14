# Original singer pilot 01

Development-only inventory profile. This is not a qualified singer, range,
recipe, recording, model or release resource. No audio or approval is implied.

## Reproducible listening experiment

```sh
cmake --build build/release --target seam_singer_pilot -j 6
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY
```

The executable renders Japanese `あ い う え お さ` across MIDI 60–72 using
the normal production export pipeline. It retains three scores and editable
recipes (baseline, 15% higher formants, breathier phonation), master WAVs,
unapproved baked candidates and a hash/peak/RMS report. It requires a new output
directory and preserves partial output on failure. These are listening probes,
not a complete corpus or evidence of intelligibility, female identity or quality.

The first complete retained local run is
`build/release/seam-pilot-listening-02/`. Run 01 is retained as a failed harness
attempt: metadata was incorrectly treated as WAV during measurement, then fixed.
The registered `seam_singer_pilot_cli` test repeats the actual render, checks
identical WAV hashes and nonzero finite levels, and checks no-overwrite behavior.

For the expanded articulation probe:

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY articulation
```

This renders `ま み む め も な に ぬ ね の ぱ た か さ` with explicit
nasal resonance/antiresonance models for `m/n`, independent burst spectra for
released `p/t/k`, and `s` frication. The recipe uses existing source semantics;
the parameter choices are experimental, not phonetic qualification. All three
variants retain 28 ordered owned gestures across 14 notes, plus score-bound
steady-pitch diagnostics. Local output: `build/release/seam-pilot-articulation-01/`.
The CLI regression checks the exact phone sequence, four gesture classes,
contiguous planned boundaries, unapproved status, audio hashes and repeatability.
Markers prove planned timing only, not perceptual onset accuracy.

Still absent from this probe: the voiced affricate `j`, pre-onset context that
reaches before its owning note, and independent intelligibility judgments. Do
not use these notes to claim complete Japanese coverage or generate a qualified
full bank. The invented Japanese inventory does name every one of those phones;
the ones with no admitted model are refused during render preparation rather
than substituted, so the inventory is not yet generatable end to end.

Generate the proposed Japanese coverage, across three planned pitch layers:

```sh
python3 tools/voicebank-script-generator/main.py --draft --profile assets/pilots/seam-pilot-01/profile.json
```

The explicit draft path emits schema 2, style-owned assignments and a
`NOT_ASSESSED` range assessment. It does not inherit the legacy profile's PASS.
`--json-output`, `--csv-output` and `--production-assignments-output` select output
files; choose new destinations to preserve prior artifacts. The assignment
document declares its requirement for the schema-4 producer writer.
Legacy inventory readers reject this new inventory rather than dropping styles.

After saving the inventory to a new file, prepare a captured definition with:

```sh
python3 -m tools.external_beta.voicebank_production prepare-draft --inventory INVENTORY_JSON --project-id seam-pilot-01 --operator-id producer --output NEW_DEFINITION_JSON
```

Use the returned SHA256 with the canonical C++ writer:

```sh
build/release/seam_voicebank_cli init-production NEW_WORKSPACE NEW_DEFINITION_JSON DEFINITION_SHA256 producer UTC_TIMESTAMP
python3 -m tools.external_beta.voicebank_production validate-workspace --draft --workspace NEW_WORKSPACE --inventory INVENTORY_JSON
```

The uppercase values are paths/hash/UTC timestamp to supply, not literal inputs.
This creates missing, unreviewed assignments; it does not register source rights
or grant permission to generate from an unapproved source. Definitions and
workspace creation use new destinations so existing evidence is preserved.

Next implementation work completes legacy migration and the generation planner,
then measures articulation and the actual voice before generating a full bank.

## Source authorization

A workspace created from a draft definition declares no source strategy, and nothing may be
imported or generated until one is registered. For this pilot the source is the repository's
own procedural recipe, declared in `source-declaration.txt`:

```sh
seam_voicebank_cli register-source WORKSPACE PROJECT_SHA256 pilot-procedural procedural pass \
  yes yes no no assets/pilots/seam-pilot-01/source-declaration.txt LICENSE_SHA256 producer UTC
```

`PROJECT_SHA256` is the sha256 of the workspace's `project.json`, and `LICENSE_SHA256`
is the sha256 of the declaration file. The declaration grants source use and transformation
and says nothing about redistribution or commercial use, so it is not a release permission.
It is a producer declaration, not legal verification, and it cannot authorize a human
recording, a TTS voice or any other source. The retained digest is captured in the workspace,
so the file must not be edited afterwards: a changed declaration is refused rather than
silently adopted.

## Campaign planning CLI

After creating a style-owned producer, select its exact planned take IDs:

```sh
build/release/seam_voicebank_cli draft-generation-campaign WORKSPACE RECIPE_JSON NEW_PLAN_JSON TAKE_ID [TAKE_ID ...]
build/release/seam_voicebank_cli plan-generation-campaign WORKSPACE PLAN_JSON PLAN_SHA256 NEW_OUTPUT_DIRECTORY
build/release/seam_voicebank_cli inspect-generation-campaign CAMPAIGN_JSON CAMPAIGN_SHA256
```

Use the hash printed by `draft` as `PLAN_SHA256`. Publication creates
`NEW_OUTPUT_DIRECTORY/campaign.json` with the same hash. `inspect` verifies its
canonical frozen inputs and batch layout; it does not verify current workspace
state or assert that work has run. Publication rejects a stale initial producer
state. Destinations must be new; failed partial output is preserved.

The draft command uses default aggregate/per-batch limits from the shared service.
It preflights the entire selected subset and fails on unsupported phones/styles
instead of silently reducing coverage. Selecting a subset is an experiment, not
completion of the inventory. No take approval or source authorization is created.
Advance one bounded batch at a time:

```sh
build/release/seam_voicebank_cli advance-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC
```

Advancement requires a held-out preflight first, because planning proves a class can be
prepared but not that it renders its own gestures or any audio:

```sh
build/release/seam_voicebank_cli preflight-generation-campaign CAMPAIGN_JSON CAMPAIGN_SHA256
```

This selects the campaign's own jobs by coverage -- every distinct phone symbol and
every distinct coverage kind must be exercised -- renders them one at a time into
`CAMPAIGN_DIRECTORY/preflight/phrase-N/`, and writes `preflight/report.json` beside the
retained dry audio and candidate metadata. It never collects, so no take is committed and
no approval is created. A bound that cannot cover the campaign's classes is refused with
the count rather than truncated, the directory must be new, and a phrase that renders
silence for audible phones, or produces no gesture for a phone its key declares, is
reported as `REFUSED` with its reason and named in `defectiveClasses`. The command exits
non-zero on a failing report so a script stops there, and the report and audio stay
behind as evidence either way. `advance-generation-campaign` refuses any campaign whose
`preflight/report.json` is missing, stale, names another campaign, or did not clear every
phrase.

+### Generated bank

The campaign that consumes this inventory ran to completion: 498 takes across the three
pitch layers, all unapproved marker-review material, with the commands, counts, measurements
and listening order in `CAMPAIGN_REPORT.md` beside the rendered preflight reading in
`PREFLIGHT_REPORT.md`.

### Declared coverage report

```sh
build/release/seam_voicebank_cli inspect-generation-coverage WORKSPACE RECIPE_JSON NEW_REPORT_JSON
```

This compiles every assignment's score and snapshot against the recipe and retains a
canonical report of what each class can and cannot prepare, with the refusal message
kept per class. It renders no audio, collects nothing and reserves no assignment, so an
incomplete result is a finding rather than a failure: the command writes the report and
exits zero, and `status`, `missingPhones`, `missingKinds` and `refusedClasses` carry
the answer. The report destination must be new.

The retained result for this inventory against the pilot's own maximal recipe is
`coverage-report.json`, summarised in `COVERAGE_REPORT.md`: 498 of 1026 assignments
prepare, 166 of 342 coverage keys, 20 of 41 phones and 5 of 8 kinds. Every remaining
refusal is an absent model or a symbol the Japanese adapter cannot resolve; the 210
vowel-to-coda placements that used to fail now prepare, because an explicit phone hint
gives an ordinary consonant its place in the syllable instead of calling every consonant
an onset.

Each invocation verifies completed receipts against repository history and
prepares/renders/collects at most one remaining batch. Repeat with the same plan
and hash until `COLLECTED_UNREVIEWED`; that status is not voice approval or Beta GO.
Retry uses original expectations and recognizes a commit whose receipt was not
written. Unexpected producer changes return a conflict. Cancellation retains
work. The initial intent-publication crash window, hard disk quotas, large-campaign
performance and process-kill qualification still need hardening; use pilot-scale
material until those checks are complete.

### Reattack versus melisma listening fixture

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY boundaries
```

This exports the same four-note melody twice: four separately articulated
Japanese `a` vowels, then one `a` continued through three `ー` notes. Each note
lasts 250 ms. Compare the first second with the second second in the baseline,
higher-formants and breathier WAVs. The split at one second is a new attack.
Projects and recipes are retained beside the audio for editing and replay.
The CLI test verifies exported dry PCM boundaries and deterministic repeats;
it does not establish intelligibility, naturalness or listener preference.

### Standalone syllabic nasal fixture

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY nasals
```

Exports `ん・あ・ん・い・ん・う` as six separate 250 ms notes. The explicit
`N` pose uses the nasal resonance/antiresonance model with full nasal coupling;
each nasal owns its entire note, without an inserted vowel. This is an audible
diagnostic model, not a qualified Japanese pronunciation. The three variants,
editable scores, recipes and unapproved candidate metadata are retained.

### User-authored phrases

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY phrase 'ま:60' 'た:64' 'ー:67' 'ん:65' 'あ:60'
```

Supply 1–64 `LYRIC:MIDI[:TICKS]` arguments, with MIDI pitches 24–96. Duration
defaults to 480 ticks (250 ms at 120 BPM); explicit durations may be 1–3840
ticks, up to 61440 ticks in total (32 seconds, or 16 bars of 4/4). For example:

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY phrase 'ば:60:960' 'ー:64:240' 'ん:65:720' 'あ:60'
```

The custom recipe includes explicit b/d/g closure models. Very short consonant
notes may still fail timing validation rather than compressing away their
closure/burst. Use the retained `.seam` project to edit other performance
controls afterward. UTF-8 and integer syntax are validated
before creating the output directory. The pilot combines its explicit oral,
nasal, frication and released-stop poses; it does not support every Japanese
syllable. Unsupported resolved phones fail during ordinary render preparation,
retain partial diagnostic inputs, and do not produce a successful `pilot.json`.
No phoneme substitution or sample approval is performed. Voice quality remains
unqualified; this is a bounded authoring/audition entry point, not a full bank.

### Voiced versus unvoiced stops

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY stops
```

Exports `ぱ・ば・た・だ・か・が` at matched pitches within each pair. The
schema-six recipe explicitly supplies closure voicing for b/d/g and preserves
the paired unvoiced release spectrum, isolating the closure difference for
listening. Candidate schema six retains `voiced-plosive` markers, recipe hash
and voiced source revision; approval remains `unapproved`. The test checks
silent versus nonzero closure PCM and repeatability, not phonetic accuracy.
This diagnostic still needs listener evaluation and parameter refinement;
prevoicing, voice-onset-time variation and natural coarticulation are not claimed.

### Affricates

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY affricates
```

Exports `つ・ち・た・さ` at 250 ms per note: the two unvoiced affricates beside
the stop and the fricative they are composed from. A schema-seven recipe pose
binds the release spectrum and the frication tail, and one gesture carries the
whole phone: a silent closure, a finite release, then frication until the vowel
nucleus. The split is deterministic (the release lasts its declared
milliseconds, the tail keeps at least 20 ms and at least half of what remains,
and the closure takes the rest), so a note with no room for a closure, a release
and a tail is refused with the millisecond requirement instead of being
compressed. The candidate metadata records schema seven, `affricateRevision`
one and `affricate` markers; the CLI regression checks the marker sequence, the
silent closure, the nonzero release and tail, and repeatability.

Voiced affricates (`じ`, `ぢ`) are not supported: a prevoiced closure and voiced
frication are a different model, and the unvoiced pair is not substituted for
them.

### Liquids and glides

```sh
build/release/seam_singer_pilot NEW_OUTPUT_DIRECTORY glides
```

Exports `ら・わ・や・あ` at 250 ms per note: the three voiced approximants beside
a bare vowel as the comparison point. A schema-eight recipe pose declares each
approximant's own resonance bank and the milliseconds of formant transition
that carries it into the vowel that follows; the transition begins exactly that
far before the gesture ends, so it lands on the vowel nucleus instead of being a
short step at the vowel's own onset. A note too short for the declaration keeps
the declaration and compresses the entry crossfade instead of dropping the
motion, and a gesture whose span is shorter than the declared transition
becomes entirely transition. The candidate metadata records schema eight,
`approximantRevision` one and `approximant` markers; the CLI regression checks
the marker sequence `r a w a y a a`, that no segment of a glide is silent, that
each glide's own span measurably changes its band profile rather than holding
one pose, and repeatability. That the motion is toward the vowel, and comes from
the declared duration rather than the gesture's existence, is asserted where the
declaration can be compared against a control, in `seam_articulation_context_tests`.

The poses are experimental parameter choices, not phonetic qualification, and no
listener has judged `ら`, `わ` or `や`. Pre-onset context that reaches before its
owning note, and coarticulation with a preceding phone in the same note, remain
open. `l`, the voiced affricate `j` and every other phone the pilot recipe does
not admit are refused with their phone name and recipe identity rather than
approximated.

## Fricative and palatalized coverage in the maximal recipe

`seam_singer_pilot NEW_OUTPUT_DIRECTORY phrase 'あ:60'` (custom phrase mode) declares every class this
pilot has, and that is the recipe the coverage report measures. It now also declares the fricatives
`sh`, `h` and `f` as unvoiced frication with their own spectra, `z` and `v` as voiced frication with the
same-phone resonance pose that source requires, and the palatalized consonants
`ky gy hy py by my ny ry fy vy` as recipe schema nine: each takes its base consonant's release and the
palatal resonance declared under its own name, so a bank has a `ky` unit rather than a relabelled `k`.
`fy` also stopped being classified as voiced, which it never was. Together these took the pilot
inventory from 498 to 948 of 1026 assignments the recipe can prepare; the remaining 78 are `j`, `R`,
`glottal`, `br`, `pau` and `cl`. Preparing a class is still not phonetic qualification: no listener has
heard any of them, and every burst spectrum is the base consonant's.

## Voiced affricate coverage in the maximal recipe

The maximal recipe also declares `j`, the voiced affricate じ/じゃ, as a prevoiced closure, its
release burst and a voiced frication tail in one gesture (recipe schema ten). Its closure carries
the excitation the score supplies, so it is audible rather than silent the way an unvoiced
affricate's closure is, and its tail adds frication noise on top of that voicing. Coverage is now
978 of 1026 assignments; the 48 that remain are the two adapter symbols with the breath sequence
(`R`, `glottal`, `br`) and the pause and closure events (`pau`, `cl`). No listener has heard any of them,
and every spectrum is a declared engineering parameter rather than phonetic qualification.
