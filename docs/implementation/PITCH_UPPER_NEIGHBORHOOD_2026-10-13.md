# Preserve valid pitch near the upper bound

Date: 2026-10-13, Asia/Seoul. Baseline: `9b1002dda9bfd0883bab651853129a1d201b859e`. This numerical follow-up supersedes the first [range candidate](PITCH_DECLARED_RANGE_2026-10-13.md). The [evidence index](PITCH_UPPER_NEIGHBORHOOD_2026-10-13.evidence.json) binds 142 retained artifacts and the predecessor manifest. Independent review and the take-inspector compatibility follow-up remain open; this is not a completed production-QC milestone.

## Reproduced boundary regression

The first candidate rounded the shortest lag up and skipped a valid boundary peak at 44.1 kHz. Native inspection of synthetic 1,199–1,200 Hz tones then selected an octave-low estimate. A new positive test failed on that candidate: eight existing cases passed and the new case failed. Its baseline binary, log and sources are retained.

Restoring only the old rounding fixed that newly introduced case but exposed an older equivalent peak-selection problem at 22.05 and 48 kHz. The final implementation includes **one lag below the original floor boundary**. This gives the first candidate peak its left neighbor for local-maximum selection and interpolation. The final inclusive frequency guard remains: out-of-range proposals are unvoiced with zero F0 and measured confidence. This is not a general guarantee against octave errors in complex singing.

The added test requires a 1,199 Hz fundamental to remain voiced and within 2 Hz at 22.05, 44.1 and 48 kHz with both Direct and FFT correlation. The existing multi-rate bound and rejected-interpolation tests continue to pass. The additional lag is charged in the exact Direct work budget before allocation, including silence. The budget test preserves its positive exact-boundary and negative one-term-too-small controls. A failed intermediate run exposed the old budget expectation; its log is retained, not described as a product regression or a first-run pass.

Native features now identify `fft-autocorrelation-v3`, acoustic analysis revision `4`. Revision `3`/features `v2` described the first candidate and cannot be relabelled as current. Python fixture consumers use the new identity and explicitly reject both v1 and v2; acoustic stale-revision coverage includes 0, 2 and 3. Old recordings, analysis outputs and manifests remain unchanged.

## Validation and exact-original measurements

The full dependent Release target rebuild completed after the library changes. A final test-only rebuild followed the budget-test correction; no library changed after the full rebuild. Retained attempts include the earlier 147.41-second full build and failing runs. The final full incremental target build took 5.13 seconds and passed; this is not a clean-build timing claim.

| Check | Result |
| --- | --- |
| Final focused CTest | 7/7 entries, 22.38 seconds |
| Native cases inside six entries | 179 pass: 57 synthesis, 26 sample-review CLI, 9 pitch, 6 take inspection, 22 production review, 59 production |
| Native training-pitch CLI entry | Pass, counted separately from native cases |
| Four Python modules | 23 reported: 22 pass, one optional native control skipped; 15.014 seconds |
| Separately enabled actual-native comparison control | Pass |
| Original ten-recording remeasurement | 20 native invocations, all exits 0; unchanged source SHA/frame identities |
| Read-only evidence reproduction | 142 artifacts plus predecessor 90+2 and original supplement 124+8 hashes pass |

The new CLI SHA is `e351b579d5c7a4559a214a32196aa2f853621a3f83c4b7e08811dc02e1fb582e`. All new voiced estimates are finite and inside 60–1,200 Hz; all unvoiced F0 values are zero. The ten original tracks go from **106 old out-of-range voiced frames to zero**. Exact recomputation of the 1,088 changed F0 values gives **106 old off-range voiced frames rejected, 976 old in-range voiced frames now unvoiced, and six changed in-range values**; 16,092 other in-range values remain identical. Of the 976 losses, 946 had old estimates between 1,100 and 1,200 Hz; they occupy 176 runs with a maximum of 23 frames. Correlation confidence is identical in 99 of the 106 off-range rejections. This changes training voicing labels and take-inspector medians; it is not a demonstration of true voicing accuracy. The [limits disclosure](PITCH_LIMITS_DISCLOSURE_2026-10-13.md) binds the exact retained frame comparisons and additional synthetic controls.

The freshly recomputed longest note-66 observation run remains 58.0498866 ms, below the descriptive pre-screen's 150 ms threshold. Windows overlap; this does not qualify a sustain, layer, vowel or usable bank. Every native feature output remains unadmitted and release-ineligible.

## Open take-QC identity and compatibility task

A separate native harness links the same take inspector first with the retained original analyzer and then with the new analyzer. Across ten originals, sixteen edge-tone controls and one format-valid 48 kHz PCM24 control, it records 54 policy cases. Twenty measurement results differ while both serializers still emit inspector version **2**. This harness is measurement evidence, not a producer import or listening test.

The format-valid 1,199 Hz control changes analyzed MIDI 74 to 86 and expected-root-69 deviation from 535.509953 to 1,735.509558 cents, with unchanged source bytes and inspector version. The expected-root assignment is a diagnostic test input; it is not a reviewed bank assignment. Both policy outcomes remain failed for that requested root, but the measurement identity still needs correction.

The next code unit must advance take-inspector identity, distinguish supported historical receipts from current measurements in native and Python validation, and preserve readable old projects and exact historical bytes. Current lookup and new approvals must not treat old receipts as current. A constant-only bump would make existing version-2 histories fail the strict decoder and may invalidate historical approval-policy validation; it is insufficient. Add actual raw/derived history and current-approval regression controls before changing the shared contract. The existing owner's failed-QC approval decision remains pending and must not be supplied by this implementation.

## Source and acceptance boundaries

The internet source intake remains 25 original clean f1 WAVs plus 272 separate female growls, 297 originals total. No sample was generated for that intake. Source licenses/provenance are recorded in the [source report](FEMALE_VOCAL_SOURCE_INTAKE_2026-10-13.md); bank rights admission, performer evidence, actual producer/distinct reviewer and per-material listening/mapping decisions remain unestablished.

The numerical repair is locally verified. Independent implementation review and the coupled receipt identity task remain open. Usable Alpha stays **0/20 recorded PASS**; External Beta is BLOCKED. All original product, language, learned-resource, installed/platform/plugin, signing, human qualification and release obligations remain. No honest overall implementation percentage is inferred from these tests. `.github`, Windows TODO, stash, original cuts/null worksheet and frozen owner kit are preserved; the full goal remains ACTIVE.
