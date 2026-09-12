# Source phoneme alignment JSON v1

This codec serializes authored source landmarks. Render snapshots discover optional sidecars and the frozen phrase pipeline consumes them for supported raw rendering. Bank content identity and signed-package installation include present per-unit sidecars. The existing voicebank manifest schema is unchanged.

## Bank and package identity

The bank content hash retains its legacy value when no matching sidecars exist. Otherwise it appends a length-prefixed `source-phoneme-alignments-v1` section with lexically sorted manifest-unit sidecar paths and their exact-byte SHA-256 digests. Empty alignment directories do not change identity. Unknown files not owned by manifest units do not participate in synthesis identity. Real directories and contained regular files are required; symlinks reject. Limits are 512 KiB per sidecar and 64 MiB aggregate per bank (snapshot reads retain the tighter 4 MiB phrase budget).

Signed-package preflight computes the same section from verified entry digests and enforces the same byte limits. The pack/install regression verifies exact sidecar preservation, identical source/installed hashes, idempotent reinstall, and loss of receipt-backed trusted status after installed sidecar bytes change. Byte identity and a trusted signature do not prove semantic alignment validity or phonetic quality; audio-bound loader validation and separate quality/admission checks remain necessary.

Required root fields, with no unknown fields:

| Field | Meaning |
| --- | --- |
| `formatId` | `com.project-seam.source-phoneme-alignment` |
| `schemaVersion` | Integer `1` |
| `unitId` | Exact owning unit ID |
| `audioSha256` | Lowercase SHA-256 of the exact encoded source audio |
| `landmarks` | Ordered objects containing exactly `phone` and integer `frame` |

Every unit phone must have one landmark in the same order. Frames refer to the decoded source sample timeline and must increase strictly inside the unit's audio bounds. The codec does not estimate frames or accept fractional frame numbers. Decoding requires the actual unit, an externally verified encoded-audio hash and decoded frame count, then reruns structural validation against them.

Limits: 512 KiB JSON, depth 4, 2,048 nodes, 256 collection entries, 4,096 parser string bytes; domain validation limits unit IDs and individual phone strings to 1,024 bytes and landmarks to 256. Encoding also checks its final serialized size so it cannot emit an oversized document that its decoder rejects.

Version mismatches, unknown fields, stale audio identity and invalid coverage/order reject. Structural validity does not prove that a frame is phonetically correct, that a producer reviewed it, or that source rights are cleared. Those claims require separate evidence.

## Snapshot discovery

For a selected unit, the sidecar path is `alignments/<sha256-of-unit-id-bytes>.json` relative to the bank root. This avoids interpreting unit IDs as paths and permits different units sharing one WAV to have distinct alignments. Absence preserves legacy behavior. A present sidecar must be a regular contained bank asset with no symlink components and must validate against the already-frozen WAV hash and decoded frame count.

Snapshots freeze each unit's alignment once, retain the verified audio digest with its decoded buffer, and hash the exact sidecar bytes into render identity v4. Changing the file afterward does not mutate an existing snapshot. Per-sidecar input is limited to 512 KiB and aggregate alignment input to 4 MiB per snapshot. Bank/package identity uses the separate contract above.

## Frozen phrase rendering

Granular stretch revision 4 also consumes internally compiled maps: inverse positions determine grain content, while compiled pitch determines grain phase/ratio. Stable/release positions and uncovered samples follow the same map. Known-unvoiced target/source positions retain mapped samples rather than pitched grains. Authored timing replaces free source drift; nondefault drift overrides reject instead of being ignored. External stretch maps cannot bypass snapshot identity. This does not qualify source-root accuracy, voiced transients, or granular continuation attacks.

Derived source voicing: timing targets retain optional resolved phoneme voicing. Map compilation transfers it onto contiguous source-phone spans bounded by authored landmarks and unit endpoints. These spans are derived render data, not new JSON fields or measured acoustic analysis. Missing labels remain unknown. PSOLA revision 8 and spectral revision 5 exclude known-unvoiced intervals from periodic/spectral overlap output and retain mapped source material there; PSOLA also avoids sampling unvoiced source positions into its grains. A null accepted target pitch can use this known-unvoiced path, but a voiced/unknown source still requires an explicitly supported conversion rather than fabrication. Timing revision is 10.

The concatenative phrase pipeline maps validated source landmarks to all retained target nucleus anchors. It uses the same renderer-selection precedence as normal dispatch: an explicit unit renderer overrides global policy, which otherwise resolves through the bank hint. Raw linear resampling and compiled-performance Classic PSOLA/SpectralClassic are supported paths. Alignment never silently substitutes raw processing for another requested renderer.

Spectral revision 4 follows the inverse source map for each analysis-window center and uses the actual mapped inter-window distance for frequency estimation. Stable/release boundaries, raw transient positions and uncovered output samples use the same map. Source-window samples clamp inside the owning unit instead of wrapping into an unrelated loop position. Endpoint/unit/output validation and external-map rejection match the PSOLA path. Normal snapshots verify both sustained C4/G4 pitches, second-vowel timing edits and restored identity. These checks are not phonetic-event or transient-quality qualification.

Raw-path limits: root-pitch rendering only, no pitch automation, default loop-print and additional-gain parameters, mono/stereo source matching the bank sample rate, and bounded source/output buffers. Every covered note must match the placement pitch. Unit gain is applied. This raw path is not pitch-preserving time stretching or listening-quality qualification; its revision remains 5.

PSOLA revision 6 uses the inverse source map to choose the nearest authored pitch mark in the available sustain marks, while compiled absolute-time pitch controls output pulse spacing. Source stable/release boundaries and raw transient fallback positions follow the map. A normal two-note aligned-unit snapshot regression verifies both sustained C4/G4 plateaus. Map endpoints must match the exact unit/output extent; maps are bounded to 770 knots, 32 Mi frames, and absolute target magnitudes at most 2^52 for double-based interpolation. Authored knots/inverse mapping are checked independently. This does not qualify phonetic event accuracy to a single sample, voiced attack/release retargeting, or unvoiced synthesis. Missing capabilities reject rather than fall back to raw. External source-map injection is rejected by snapshot creation; maps are compiled from frozen resources internally.

Selection and timing accept optional borrowed `SourceAlignmentEvidence`: the authored alignment, externally verified WAV digest and actual decoded frame count. Full unit/phone coverage and structural validation are checked before permitting an otherwise unsupported interior edit. The frozen pipeline supplies this evidence to timing. Unit selector revision 4 requires all nuclei to be alignable during normal snapshot selection; timing revision 9 validates automatic ends after all edited nucleus dependencies resolve.

Normal snapshot selection excludes unaligned multi-nucleus candidates, chooses smaller compatible units when available, and rejects incompatible forced units. The frozen pipeline independently rejects manually supplied unaligned multi-nucleus plans. Resource-free selector callers retain legacy planning behavior by default; a plan from that API is not proof of render capability. This fallback does not replace the aligned long-unit path or the future pitch-preserving singer backend.

Before selection, snapshot creation probes enabled, matching-style, phone-matching units whose covered span has multiple nuclei or an interior timing edit. Present sidecars and their audio are validated and frozen through the same loader used for selected units, sharing aggregate audio/alignment budgets. Absent sidecars do not trigger candidate WAV loading. Invalid present candidate resources reject; they are not silently ignored. Selected resources reuse those frozen bytes, avoiding a second read after selection. Only selected assets enter the resulting snapshot. A normal phoneme-command regression verifies second-vowel movement, undo/redo audio and content identity, and disk-cache replay both within one note and across two adjacent same-pitch notes. This does not prove whole-melody pitch following. Bank/package identity is covered separately above.
