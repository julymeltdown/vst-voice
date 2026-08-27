# Native Editor Design Completion Evidence

Implementation candidate:
`af5a1d8f95fad33f03b5ae56ccf8158c7574c6dc`

Verdict: **100% native-editor design rubric complete.** This is not a Usable
Alpha, External Beta, RC, or GA promotion.

## Completion matrix

| Area | Candidate evidence | Verdict |
| --- | --- | --- |
| Unicode and overflow | Bounded text rasterization, CJK/combining tests, suppressed misleading emoji fallback, full semantic note value | PASS |
| Note density | Independent `+2` badge, bounded five-member panel, stable row order, visible selection cycling | PASS |
| Adaptive lanes | Shared persisted geometry, 150 ms injected-clock transition, immediate Reduce Motion final state | PASS |
| Voice and character identity | Exact card/content/binding match gates the bundled Character 01 portrait; mismatch suppresses it | PASS |
| Responsive hierarchy | 480/720/960/1188/1280/1440 viewports at 1x/2x and four zooms; toolbar identity regions remain collision-free | PASS |
| Recovery and accessibility | Shared paint/pointer/semantic controls; writable native note value and complete focused CJK detail | PASS |
| Performance | 10,000 notes, 2.19263 ms p95 against 16.7 ms, zero audio underflow | PASS |
| Independent visual review | Two fresh reviewers, 64/64 images inspected, zero skipped, zero blockers | PASS |

## Exact-SHA automated gates

```text
Release CTest                    66 / 66 PASS
Native executable               425 / 425 PASS
Phase 11 explicit                 3 / 3 PASS
ASan + UBSan                    63 / 63 PASS
ThreadSanitizer                 63 / 63 PASS
Tracked source closure                 PASS
Whitespace / patch integrity           PASS
```

The Release run took 70.46 seconds, ASan/UBSan 333.16 seconds, and TSan
880.35 seconds. No sanitizer or race diagnostic was emitted.

The native paint benchmark reported:

```json
{
  "projectNotes": 10000,
  "visibleNotes": 50,
  "averagePaintMs": 2.03028,
  "p50PaintMs": 1.98771,
  "p95PaintMs": 2.19263,
  "paintBudgetMs": 16.7,
  "paintBudgetPass": true,
  "textCacheEntries": 24,
  "textCacheHits": 1416,
  "textCacheMisses": 24,
  "underflowFrames": 0
}
```

## Rendered and native evidence

Fresh session-local packet:
`/tmp/project-seam-visual-qa-approved-af5.BfVHEj`

- `manifest.json` is bound to the candidate SHA and records 48 deterministic
  matrix captures plus 13 deterministic journey frames.
- The capture command ran twice and required identical SHA-256 values.
- `native-manifest.json` is bound to the same SHA and exact Release bundle.
- `native/rest.jpg`, `native/long-note.jpg`, and
  `native/focused-note-detail.jpg` are valid 1187 x 768 JPEG captures.
- `native/accessibility.txt` records the complete value
  `가나다라마바사 こんにちは世界 中文歌词` for both the focused detail and Note.
- The AppKit journey used an isolated application-support root, created a Note
  through a native double-click, set its lyric through the native accessibility
  value action, traversed Tab focus, then discarded the temporary document.

The packet is supplemental session evidence. The reproducible capture source is
tracked at `scripts/capture_native_ui_design_matrix.py`; no build or capture
artifact is required by tracked-source closure.

## Independent visual gate

Fresh Pass A and Pass B reviewers independently inspected all 64 images on the
candidate SHA. Both returned `PASS`, high confidence, zero skipped captures,
and no blocking finding. They explicitly verified:

- the readable badge and full stable overlap cycle;
- complete bounded Korean, Japanese, and Chinese detail;
- the actual bundled matched portrait and mismatched suppression;
- lane and identity start/mid/end frames plus Reduce Motion final geometry;
- identity suppression at 720 px, isolated identity at 960 px, and project
  metadata ending before identity at 1188 px and above.

The consolidated gate report is retained at
`.omo/evidence/project-seam-af5-gate-review.md`.

## Deliberate non-claims

This record closes U9 of the native-editor design completion plan only.
Physical Narrator observation, independent accessibility certification,
rights-cleared production character art, release signing/notarization,
commercial-host certification, external musician evidence, and every separate
Usable Alpha or External Beta acceptance requirement remain fail-closed.
