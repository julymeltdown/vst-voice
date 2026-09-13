# Pilot 01 held-out preflight

The rendered counterpart of the coverage report. Seventeen held-out phrases, one per
required phone and per required coverage kind, rendered through the ordinary immutable
generation path before any batch of the bank runs. Engineering evidence only: this is dry
candidate audio from an unqualified recipe, no listener has judged it, and nothing here is
an approval or a release decision.

## Result

| Measure | Value |
|---|---|
| Phrases rendered | 17 |
| Produced | **17** |
| Refused | **0** |
| Phones required | 20 |
| Kinds required | 5 (cv, vc, vv, sustain, special) |
| Peak range | 0.011 to 0.157 |
| Nonzero coverage | 90% to 100% of 24000 frames each |

| Class | Gestures | Peak | Nonzero |
|---|---|---|---|
| special:N | N | 0.1571 | 100.0% |
| cv:m:a | m, a | 0.1439 | 100.0% |
| cv:s:a | s, a | 0.1009 | 100.0% |
| cv:n:a | n, a | 0.0987 | 100.0% |
| vc:e:ch | e, ch | 0.0809 | 95.2% |
| cv:ts:a | ts, a | 0.0801 | 95.0% |
| cv:y:a | y, a | 0.0747 | 100.0% |
| cv:w:a | w, a | 0.0571 | 100.0% |
| vv:i:o | i, o | 0.0550 | 100.0% |
| cv:p:a | p, a | 0.0531 | 90.0% |
| cv:b:a | b, a | 0.0531 | 100.0% |
| cv:k:a | k, a | 0.0482 | 90.0% |
| cv:g:a | g, a | 0.0482 | 100.0% |
| cv:d:u | d, u | 0.0429 | 100.0% |
| cv:t:a | t, a | 0.0413 | 90.0% |
| cv:r:a | r, a | 0.0266 | 100.0% |
| sustain:a | a | 0.0110 | 100.0% |

Two readings, offered as measurements rather than thresholds. The stop classes sit at 90%
nonzero because their closure is deliberately silent for about 60 ms of a 500 ms unit, which
is the behaviour the marker analysis asserts. `sustain:a` is the quietest class by a factor
of about 2.4 below the next quietest and about fourteen below the syllabic nasal; a sustained
vowel has no consonant to raise its peak, so this is not yet evidence of a defect, but it is
the first context to listen to.

## Two real defects the gate caught

The first preflight run refused five classes, and the second refused three, both times with
candidate-metadata errors rather than audio problems. Both were latent defects in the
collector that no earlier test could reach, because no earlier test ran a generation job over
a recipe that declares more than one articulation family.

1. **The loader demanded one fixed field union per schema version.** The export writes the
   revision field for each gesture family a candidate actually rendered, so a version-seven
   affricate candidate carries `plosiveRevision` and `affricateRevision` (22 fields), and a
   version-eight approximant candidate carries only `approximantRevision` (21 fields). The
   loader asserted 23 and 24. It now admits the closed set of defined fields, bounded per
   version, so a candidate cannot invent a field while a legitimate union is accepted.
2. **A version-eight candidate was required to carry a release revision it never used.** The
   plosive revision is mandatory for versions four through seven, whose own gesture is a
   plosive family, and is required of a version-eight candidate only when it actually renders
   one of those gestures.

Both are now covered by a regression that renders an inventory of `cv:s:a`, `cv:ts:a` and
`cv:y:a` from a single multi-family recipe and requires every candidate to load.

## Where the evidence is

The campaign, its seventeen rendered phrases, their candidate audio and metadata and the
canonical report are retained under `build/pilot-01/campaign-registered/` (build output is
not tracked). The report itself is
`build/pilot-01/campaign-registered/preflight/report.json`, bound to campaign
`c312a74573ca43d9ff235a63480d49665c8e20e626d2a16175291a9c93791053`, producer
`365e8bfd36c4cd9b21558765617d56995e9a58e7ad2b16c87b5c92ec58c70dee` and recipe
`e759b55fa313e96db776c6ab777fc591a699c31b410c850150f20d4623d5c601`.

Reproduce it after registering the pilot's own source (see `README.md` and
`source-declaration.txt`):

```sh
build/release/seam_voicebank_cli preflight-generation-campaign \
  build/pilot-01/campaign-registered/campaign.json CAMPAIGN_SHA256
```

## What this does not establish

The preflight proves that the classes the campaign declares render their own gestures with
audible, bounded, reproducible audio, and that the campaign may now advance. It says nothing
about intelligibility, naturalness, female identity or musical usefulness, and the retained
audio is dry candidate export rather than a reviewed take. No take was committed by this run.

