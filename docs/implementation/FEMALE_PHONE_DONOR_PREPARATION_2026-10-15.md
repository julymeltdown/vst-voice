# Source-bound English phone donor preparation

Date: 2026-10-15, Asia/Seoul. Integrating base:
`2b9db5725aa6e1b5439d24e727bca26ccf3a7186`.
This unit implements an unassigned phone-span preparation path and runs it on
four originals from the [female SLT intake](FEMALE_ARCTIC_SLT_INTAKE_2026-10-15.md).
No new recording is downloaded, no source permission is granted and no production
workspace is created.

## Implementation

`tools/voicebank_source_intake/prepare_phone_donors.py` takes an exact, hash-bound
`SourceLabelDonorPlan` containing a license, speech WAVs, automatic FestVox label
files and explicitly requested one-to-three-label spans. It preserves the raw
source phone strings, opaque label column and original decimal endpoints. It
does not use the sustained-vowel pitch cutter as a consonant detector and does
not translate `jh`, manufacture stress labels or assign a pitch layer.

The source-label format and `AUTOMATIC_UNREVIEWED` state are required declarations,
not authenticated label provenance or phonetic acceptance. Every source/span is
validated before output creation: exact SHA-256, relative paths without traversal
or symlinks, unique source/request IDs, bounded input/output sizes, standard mono
PCM16 format, full RIFF container/chunk extent and PCM geometry, nondecreasing
bounded label endpoints, and nonempty selected frame spans. Missing/unknown plan,
source or span fields are rejected; caller-supplied approval fields cannot become
output decisions. Shared hash/path/JSON checks come from `prepare_sustain_cuts.py`.

The interval is half-open. Both endpoints use exact decimal multiplication and
ceiling to select the sample points satisfying `start <= frame/rate < end`.
Adjacent labels share the same integer boundary. This avoids rounding a fractional
boundary differently at the start and end, which would duplicate or drop PCM
samples. It does not claim the source aligner's boundary is linguistically correct.

New WAVs contain byte-exact original PCM intervals without normalization, resampling
or pitch shift. Ancillary chunks are removed with a retained change notice. Each
proposal carries source WAV/label hashes, annotation indices, original frame
coordinates, raw phones, copied PCM hash and derived WAV hash. Output is explicitly
unassigned and unapproved, with phonetic/pitch/listening reviews NOT_REVIEWED.
Silent labelled spans may be copied and flagged `nonzeroPcm: false`; that field
tests zero PCM only, not audibility. An interrupted write leaves INCOMPLETE inside
the exclusively created output folder. Existing output is refused and preserved.

## Checks and actual material

Sixteen new test methods exercise exact PCM and fractional boundaries, unchanged
annotations, deterministic repeats, stale file bindings, malformed times/RIFF
geometry, specific duplicate/missing/unpadded chunk refusals, empty/out-of-range
spans, duplicate requests, unknown sources, injected approval, malformed/deep JSON,
path/symlink controls, occupied output, explicit incomplete writes, total bounds,
ancillary chunk removal and a silent pause without approval.

The CMake intake entry now runs the new module alongside the existing sustain
suite: **31 methods pass**, comprising 16 new donor methods and 15 existing sustain
methods, including the existing actual native analysis/cut integration. The
focused registered CTest run passes in **1.018922 s**. These are Python test methods,
not 31 native or human journeys.

An initial full untargeted Release build returned exit 0 after CMake regeneration
and dependent relinking. Its tool-stream capture is truncated and does not retain
a source/binary before/after receipt or exact elapsed build time. The separately
captured full build-current check returns exit 0 in **0.128400 s**, reporting no work
to do. The subsequent CTest, preparation and native-reader receipts have stable
source/binary bindings. This is local execution evidence, not authenticated build
or interpreter dependency closure. No C++ source/header changes in this unit.

The actual plan chooses a deterministic greedy raw-symbol subset of the SLT
corpus, using lexical source IDs to break ties. It selects `arctic_a0239`,
`arctic_a0219`, `arctic_a0136` and `arctic_a0090`, then requests every non-pause
single label and every adjacent non-pause pair in those files. This selection
maximizes observed raw symbol presence; it makes no pronunciation or singing
quality decision. Source copies match their unchanged originals byte-for-byte.

| Observation | Actual result |
| --- | --- |
| Donor proposals | 312: 159 single labels and 153 adjacent pairs |
| Raw non-pause symbol presence | All 40 observed source symbols, including unchanged `jh` |
| First/repeat execution | 0.091420 / 0.085670 s; every output byte matches |
| Summed copied span duration | 36.9125 seconds, including overlaps |
| Unique selected source duration | 12.825 seconds; this is the non-overlapping original material |
| Cut size | 300–8,100 frames at 16 kHz (18.75–506.25 ms) |
| Actual native `inspect-wav` | 312/312 decode, finite, non-silent and geometry checks pass; zero reported clipping |
| Native inspection time | 1.978927 s; exact executing binary retained |

Short speech fragments and automatic symbol/pair counts are not stable vowel
loops, musical pitch layers or qualified CV/VC/VV coverage. No F0 is inferred from
these labels. The `axr`, explicit stress, singing style and reviewed boundary gaps
from the source intake remain unresolved. Derived WAVs and repeat copies are not
new source acquisitions and do not increase the 1,429-original census.

## Integration gap and next development

`libs/seam-voicebank-production/src/draft_inventory.cpp:143` and
`tools/voicebank_script_generator/draft_inventory.py:54` deliberately reject
languages other than Japanese. The current `new-producer-workspace` recording
inventory therefore cannot start an English producer journey, even though the
English phonemizer and sample draft command exist. Removing that guard alone
would apply Japanese vowels, release/special tokens and coverage rules to English.

The next coherent implementation must add explicit English profile/sequence
semantics across native and Python generation, exact regeneration validation,
normal producer creation/reopen and operator prompts. It must preserve old
Japanese document hashes, distinguish the frozen English vocabulary and declared
stress policy, and report incomplete source coverage. This is a production-path
gap; unassigned donor preparation does not close it. Real producer registration,
source policy and distinct human review remain required for actual import/review.

The [evidence index](FEMALE_PHONE_DONOR_PREPARATION_2026-10-15.evidence.json) binds
**667 artifacts / 14 preserved inputs** under
`out/evidence/female-phone-donors-2026-10-15`. A read-only verifier independently
recomputes all 312 frame intervals using rational arithmetic, compares their PCM
with the original copies, verifies repeat bytes, source hashes, receipts and
canonical pins. `verify.py` itself runs; no acquisition/runtime script or native
binary runs during that audit. Use `--source-commit` after integration to compare
the held source snapshots against immutable Git rather than later working files.

Independent technical review of this implementation is PENDING. Exact a30f7969
scorer review has become APPROVE with no findings; its full verdict is captured
for a separate metadata unit. The separate SLT source-intake review is now active.
Neither grants production/source/personality/actor/listening acceptance. Alpha
remains 0/20 recorded passes, U45 held, Beta BLOCKED, full goal ACTIVE. `.github`,
README Windows TODO, original sources, earlier frozen evidence and stash remain
preserved.


### Later English producer-entry implementation

The Japanese-only entry gap above is addressed by [English producer workspace](ENGLISH_PRODUCER_WORKSPACE_2026-10-15.md): explicit English inventory/contract, native/Python parity and normal Studio/CLI creation/reopen. This does not import or approve these donor cuts; actual actors, source policy, downstream bank contract integration and singing coverage remain open.


### 2026-10-15 — Exact SLT intake review and explicit attribution follow-up

Independent review APPROVED exact 2b9db572 for engineering/source-intake accuracy, with no findings or required changes. The complete verdict is retained in [the attribution follow-up](FEMALE_SLT_ATTRIBUTION_2026-10-15.md) and its evidence index. Its optional code-citation notes are corrected; fresh donor preparation carries Alan W Black and Kevin A. Lenzo’s names beside unchanged COPYING and modification notices. The new helper/packet passes 32 focused intake methods; all 312 donor WAV bytes and proposal rows match the prior root, which is preserved. This later implementation has no independent verdict yet; faf5 donor-code and ecee English-entry reviews are separate. No new original audio, source/performer rights, production actors, listening or bank approval. Census 1,429; Alpha 0/20, U45 held, Beta BLOCKED.
