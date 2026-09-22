# U6 neural musical-control parity

Date: 2026-09-22. Baseline: `7513d3e47bbf96bfcf9fdd2569e696169c15c0f5`.
Scope: shared musical-envelope authority and neural continuation conditioning.
The complete U6 and full-product Beta contracts remain open.

## Audit findings reproduced before repair

The U6 plan requires complete melody, held-vowel versus repeated-syllable
articulation, staccato/release, and absolute-time modulation to be consumed by
all three rendering families. Current source already contains sample,
procedural and neural consumers; older chronological entries saying those
families are absent are not a current inventory.

Two neural integration gaps were reproduced:

1. An explicitly extended phoneme retained its owning note's edge pitch and
   dynamics, but the adapter replaced articulation gain with unity outside the
   score note. A completed staccato or accepted-release gate therefore reopened
   at note end. Two new regressions failed: 26 existing cases passed and two
   failed. This was a request/output-envelope defect, not a listening opinion.
2. Same-pitch repeated `a` syllables and an `a` held across notes had identical
   acoustic token/duration boundaries even though the compiler distinguished
   their reattack intent. A third regression failed after the envelope repair
   (28 passed, one failed). A changed request identity cannot itself communicate
   this distinction to the acoustic model's token/duration inputs.

Red logs: `build/debug/u6-tail-red-{build,ctest}.log` and
`build/debug/u6-melisma-red-{build,ctest}.log`. The initial tail-test build also
warned about two out-of-order designated initializers; their field order was
corrected before strict Release verification.

## Shared envelope closure

`ScoreNoteSpan::closesPhoneticTail` is derived once after accepted/manual
ownership and continuation have been resolved. Staccato closes its tail;
a positive effective accepted release closes a non-continuing tail. Neutral
release, manual replacement and a genuine continuation do not create a new
closure. The shared evaluator uses this state through score gaps.

The neural adapter now uses the same state of the phoneme's owning note after
its end. A following note's open envelope cannot reopen the preceding phone's
closed tail. Inside the note, sample-domain dynamics and articulation are still
read from the shared evaluator. Normal preutterance and unclosed phonetic
extensions retain their previous edge pitch/dynamics behavior. Timbral
ownership continues to use actual time, as established by the earlier review.

No additional per-frame compiler array is introduced: this is one derived flag
per bounded note. Neural request arrays retain their existing frame budgets.

## Held vowels reach the acoustic model

The adapter first evaluates every original phone's frame controls with its
original owning note. Only afterward does it join acoustic token spans that
satisfy every condition below:

- Both are voiced, non-silent nuclei with the same vocabulary token.
- The shared compiler marks the incoming note as a continuation, not a reattack.
- The two owning score notes and the two phonetic spans are contiguous.
- The joining phonetic boundary is exactly the incoming score-note boundary.

Repeated syllables, staccato, changed vowels, rests and off-score timing
boundaries therefore remain explicit. Multi-note continuation chains retain
their changing melody; joining tokens never substitutes the first note's F0,
dynamics or timbral ownership for later notes. The bounded pass can only reduce
the number of spans and checks cancellation. Existing vocabulary IDs and wire
schema are unchanged; no invented phone or model input is introduced.

This transfers articulation intent to the model input. It does not prove that a
particular trained model produces an acceptable natural onset or held vowel.

## Identity and compatibility

- Performance compiler revision: 20 (was 19).
- Neural score-request algorithm revision: 3 (was 2).
- Existing sample/procedural/neural snapshot identity already includes the
  compiler revision; neural identity also includes the request revision.
- Project schema, model weights, vocabulary and wire schema are unchanged.

Affected renders must be recomputed under the new identities. Historical
listening packets and model receipts are not rewritten or promoted. No training,
model replacement or listening qualification was performed for this repair.

## Verification coverage

Four added cases in `tests/test_neural_worker_protocol.cpp` cover:

1. Staccato remains closed through explicit coda extension; preutterance is
   retained. Actual output finalization applies the requested gain once.
2. Positive release closes the prior phone's tail even while the next score
   note is active. Zero release and manual Replace preserve unclosed tails;
   the following note keeps its own open gain.
3. Repeated versus held vowels produce distinct acoustic token/duration inputs.
   C4-G4-E4 retains all three pitch plateaus in one continuation token. Changed
   vowels, staccato, gaps and shifted phonetic boundaries do not get merged.
4. At 8/44.1/48/192 kHz, a nonzero region origin, tempo change, vibrato and gain
   ramp reach every neural input frame. Finalizing a constant test carrier
   matches the shared post-render gain operator evaluated in reverse
   127/733/1024-frame blocks.

The first, second and fourth cases run at all four rates. Compiler regressions
also assert derived closure for staccato/release and its absence for manual,
neutral and continuation cases. The gain comparison uses a controlled carrier,
not a learned vocoder: it is exact control/finalization evidence, not a new
full-audio or perceptual comparison of three renderers.

Fresh builds and all eleven selected CTest targets pass on the complete tree:

- Release, warnings-as-errors: 1,027 case executions in 25.66 seconds, including
  842 monolithic cases.
- Debug: 1,158 case executions in 164.78 seconds, including 973 monolithic cases.
- In each configuration the other suites contain 30 neural protocol, 22 compiler,
  48 performance snapshot, 39 voice-design, seven neural snapshot, four neural
  runner, six neural workflow, four neural selection, 21 coordinator and four
  original-singer journey cases. Counts overlap across suites, not unique coverage.
- Captured-teacher preparation Python tests: 12/12, no skips.
- Source closure and staged whitespace checks: PASS.

Logs: `build/debug/u6-controls-final-{build,ctest}.log` and
`build-u4-macos/u6-controls-final-{build,ctest}.log`. Earlier focused runs and
red regressions are retained separately. Debug has warnings-as-errors disabled;
the production Release build is strict. Independent exact-commit review remains
pending. The installed singer journey is the automated development fixture,
not unaided creator, DAW, platform-distribution or perceptual acceptance.

## U6 criterion map and remaining boundary

| U6 criterion | Current implementation and executable evidence | Limit |
|---|---|---|
| Whole melody independent of selected units | Shared compiler; normal aligned long-unit PSOLA/spectral/stretch snapshot tests; measured procedural phonation C4/G4; new neural multi-note F0 and continuation-input tests | Neural input correctness is not measured learned-singer pitch/quality acceptance |
| Continuation, reattack, staccato and release | Existing compiler/classical/procedural envelope consumers; repaired neural tail ownership and token-boundary distinction | Natural transitions and qualified singer behavior still require resource/model evidence |
| Tempo, absolute vibrato, block subdivision and neutral gain | Existing compiler and snapshot/checkpoint tests; new multi-rate absolute neural-input and shared-gain comparison | Controlled-carrier parity is not a listening or host-performance result |
| Bounded shared musical authority | Bounded immutable note/curve/timing compiler; all three factory paths consume it; derived note-tail flag adds no song-length arrays | This increment is not a full memory/performance or all-expression audit |

This closes two concrete integration defects, not all U6/R1/R2/R15 obligations.
No complete roadmap unit, qualified neural singer, Windows support, installed
host result or Beta GO is counted from these checks. The full R1-R20 objective
and required external/perceptual evidence remain unchanged.
