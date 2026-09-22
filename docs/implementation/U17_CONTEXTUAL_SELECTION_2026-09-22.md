# U17 contextual source-unit selection

Date: 2026-09-22. Baseline: `4c733fb913b2b0030ea918ab62d7b3e1d94a5572`.
This implements the contextual-search portion of U17 through the normal sample
snapshot/render/inspection path. It does not accept all U17 or qualify a singer.

## Changed behavior

The former selector kept one cheapest chain per token boundary. That loses a
slightly more expensive predecessor whose source ending matches the next unit
better. It also used a large negative local score for forced units: a long unit
starting earlier could jump across a forced start when the constrained path was
otherwise unavailable.

The selector now keeps one state per **candidate occurrence** (start, span,
unit, renderer intent), with the cheapest complete predecessor chain for that
occurrence. Incoming edges compare the preceding source's tail and the next
source's head. The edge depends only on those occurrences, so dominance is exact
for this objective. There is no beam or silent candidate pruning. Limits cause
an explicit refusal. Exact score ties use canonical occurrence index, then the
already-resolved predecessor chain in reverse occurrence order; manifest order
does not change the result.

Forced intervals are validated before search. Overlapping/duplicate intervals
fail; adjacent intervals are legal. Every nonmatching candidate intersecting a
forced half-open interval is removed, including a candidate that starts before
the interval. A successful complete cover must therefore contain each forced
unit and preserve its renderer choice. Missing constrained coverage fails rather
than substituting the unconstrained long unit.

## Acoustic objective: an explicit source-domain proxy

`analyzeUnitJoin` measures the first and last **20 ms** inside
`[audioOffset, audioEnd)` of the playable source crop. Short units use their
entire crop. It averages channels arithmetically without changing source audio.
Measurements are derived from the same decoded, byte-hashed buffers later used
for rendering, never from unverified cached analysis files.

Each boundary stores:

- RMS level in dB including unit gain, floored at -180 dB; exact silence stays
  at the floor regardless of gain.
- Four normalized autocorrelations, using delays corresponding to 1, 2, 4,
  and 8 frames at 48 kHz, rounded to source-rate frames with a minimum of one.
  Insufficient or silent comparison windows have zero correlation. There is
  no resampling or waveform phase shift in this measurement.

The incoming cost is `0.5 * min(24, abs(tailDb - headDb))` plus the sum of
absolute differences of the four correlations. It is added to the existing
pitch-distance, span, priority and take cost. A score rest resets the acoustic
edge. The first unit has no incoming acoustic edge. A silent pair has a defined
zero mismatch, not a special quality reward or approval.

**This is not a perceptual score, acoustic phoneme alignment, or a prediction of
the post-pitch/time-map/post-seam waveform.** Source phase inversion is not
identified by autocorrelation. The pair compositor retains its independent
cancellation check. Natural-voice corpus/listening qualification and any future
scoring calibration remain open. Selection revision **2** versions extraction,
scoring weights, hard constraints and deterministic tie behavior together.

## Frozen decision identity and resource limits

The normal snapshot first probes matching units' alignment eligibility, then
enumerates eligible candidate occurrences, freezes every competing source,
measures it, and runs the contextual search. Missing/decode-invalid competitors
are explicit errors; they cannot disappear from the optimization silently.
The existing per-file frozen map deduplicates decoded assets. Winners use those
same buffers; losing buffers need not remain allocated after snapshot creation.

A canonical decision digest includes ordered matching-unit metadata,
alignment availability and hashes, eligible candidate occurrences, all
competing audio/sidecar hashes, styles, and selection revision. It enters the
render identity even when changing a losing competitor leaves the winner and
PCM unchanged. `UnitPlan` and each selected entry retain the digest. Owned-output
children retain the exact full-context decision; cold and cache-hit region and
project results publish the same plan/rationale.

One budget object is shared by alignment probing, preliminary enumeration,
measurement and actual search for **both** style arms:

| Work | Hard ceiling |
|---|---:|
| Tokens | 4,096 |
| Inventory units | 65,536 |
| Charged matching/comparison work | 8 Mi operations |
| Enumerated candidate occurrences, including preliminary enumeration | 65,536 |
| Retained candidate states, aggregate | 65,536 |
| Ending states at one token boundary | 256 |
| Expanded predecessor edges | 4 Mi |
| Analyzed interleaved boundary samples | 16 Mi |
| Charged copied candidate/alternative/rationale identity bytes | 8 MiB |

IDs are bounded to 512 bytes and unique. Fixed-size autocorrelation processing
adds a constant factor to the bounded window samples. Existing aggregate
encoded/decoded audio limits (256/512 MiB) and alignment limit (4 MiB) remain
shared across arms; decoded-sample limits are now also passed into WAV decoding
before allocation. Cancellation is checked during matching, measurement, edge
expansion and source freezing, and passed from the region renderer into sample
snapshot construction and WAV decode. File reads retain the existing held-input
boundary; this does not claim interruptible operating-system file calls.

Both styles are optimized independently. If their selected token partitions or
timing are incompatible, the existing pair admission refuses. It does not
silently choose a worse primary or secondary path merely to manufacture a pair;
the endpoint-equals-standalone contract is preserved.

## Inspection

Selected entries carry mode, occurrence, predecessor ID, local cost, incoming
edge cost, cumulative cost, forced status, revision and decision hash.
`describeUnitSelection` formats that actual decision. The unit-lane model and
standalone/CLAP sample-microscope context expose it; full context is available
in existing accessibility semantics. The CLAP microscope captures the exact
selected occurrence when opened, not the first occurrence of the same unit ID.
Fallback inspection without a production snapshot is labelled **Metadata-only**.

The existing microscope header is width-truncated on narrow windows. This
increment does not claim a new expandable rationale panel, complete native
visual/accessibility acceptance, or scored global counterfactual alternatives.
Forced-selection alternatives remain metadata choices, not scored competitors.

## Verification and review

The dedicated contextual suite exercises globally better predecessor and
outgoing-join choices, repeated occurrences, manifest permutations, forced
jump-over/overlap/adjacency, exact and exceeded work/state budgets, cancellation,
silent/short/stereo/nonfinite/cropped inputs, score rests, missing evidence,
losing-source mutation/deletion, alignment eligibility mutation, immutable
whole/owned rendering, and cold/cache/inspection rationale.

A new three-placement style-pair regression uses different per-style attack and
release envelopes. It checks standalone-equal endpoints through the production
project renderer and float-WAV writer, cache identity/rationale, and complete
whole-versus-owned midpoint PCM equality including seam samples. This is a
render/file-writer test, not a live Export-dialog or installed-DAW journey.

Strict Release and Debug selected-target builds pass. The CLAP editor library
also builds in Debug (the Release configuration has that plugin disabled).

| Suite | Release | Debug |
|---|---:|---:|
| contextual unit selection | 11/11 | 11/11 |
| paired style blending | 11/11 | 11/11 |
| performance snapshots | 48/48 | 48/48 |
| synthesis/voicebank/conditioning regression | 34/34 | 34/34 |
| monolithic regression | 831/831 | not rerun |
| authoring render coordinator | 19/19 | not rerun |
| installed original-singer song journey | 4/4 | not rerun |

Release passes 7/7 CTest targets, 958 case executions, in 22.27 seconds. Debug
passes 4/4 targets, 104 cases, in 27.71 seconds. Counts are suite executions,
not deduplicated coverage or quality qualification. Logs are
`build-u4-macos/u17-context-final-{build,ctest}.log` and corresponding paths under
`build/debug/`. Case-level output is in each `Testing/Temporary/LastTest.log`.
Early strict builds caught a nonexistent cancellation enum spelling and missing
`<set>` include; those were corrected without relaxing diagnostics or tests.

Developer 2 REQUESTED CHANGES at `9d40f0344aadbd0a678643177156b83be2592b4a`.
All four focused binaries passed independently in Release and Debug (104 cases
each). The sole blocking finding was explicit-refresh measurement freshness,
described below; no other blocking selection/pair finding was established.
No new live AppKit/installed-host journey or listening run is claimed.

### Explicit-refresh rework

The reviewer traced a P2 gap: refreshing the bank catalog did not invalidate
`acquireCurrent()` or `AudioMeasurementCapture::matches()`. The catalog could
discover that a losing competitor changed, yet the native measured-output panel
would still accept the previous publication as current evidence. This was not a
stale WAV-export bug: export already re-resolves and renders afresh.

A connected regression reproduced the gap on the unfixed implementation:
`seam_u3_standalone_tests` passed four cases and failed the new case because the
old current request ID survived refresh (`u17-refresh-red-test.log`). The private
browser refresh is reached through the public successful install of another
signed bank, after modifying an unselected source in the selected development
bank. No track selection or project-revision change is involved.

`AuthoringRuntime::invalidatePreview` now clears pending debounce requests and
revokes current publication authority under the same lock used for debounce
dispatch. `requestPreview` does this before source resolution, including when
resolution returns no request; an obsolete queued request cannot later restore
current status. Explicit browser refresh invalidates/cancels before catalog I/O
and requests a newly resolved preview only after successful refresh. Failure or
an unresolvable exact bank leaves previous PCM historical, not current evidence.

The regression also drains a cancelled debounce window, requires immediate
current/capture revocation, checks the unchanged document revision and now
unresolved bank, and verifies that historical captured PCM remains unchanged.
After rework, Release passes the 832-case monolithic suite, 19 coordinator cases,
four original-singer journeys and five standalone workflow cases (21.49 seconds
for those four targets). Debug passes the five standalone and 19 coordinator
cases (5.00 seconds). The final expanded debounce assertion is included in the
fresh Release monolithic and Debug focused binaries. The final Release focused
rebuild/rerun also passes all five cases (0.77 seconds), with the expanded
debounce assertion included.
Logs: `u17-refresh-{build,final-ctest}.log` under each build directory, plus
`build-u4-macos/u17-refresh-focused-{build,ctest}.log`.

Independent re-review of this repair is pending. General filesystem watching is
not introduced or claimed. The reviewer also noted a nonblocking efficiency
follow-up: cache adjacent-note/rest lookups per boundary instead of scanning
notes per expanded edge; no measured performance failure was reported.

## Still open

U15's broader shared acoustic-analysis/conditioning contract, U16's natural-voice
quality evidence, resource-specific blend/range/listener qualification, expanded
rationale presentation and any broader promised resource-matrix behavior remain
required. The original neural singer, language/resource acceptance, installed
release evidence and full Beta GO have not been waived. No additional complete
roadmap unit is counted here.
