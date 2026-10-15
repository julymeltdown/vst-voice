# Prepared donor PCM range coverage — 2026-10-15

The snapshot tests now separate canonical PCM range refusals from the interval-length and duration equalities. This locally repairs the test gap raised in the exact `24cf12d5` review. Runtime code, schemas, QC thresholds and approval policy are unchanged. Independent closure awaits review of this exact test commit.

## What the earlier test could miss

The existing nine malformed forms also violate interval or duration equality. They remain useful, but would all fail even if the range limits were removed. The new two-donor fixtures keep those equalities exact after JSON serialization, rehash the packet and retain its original plan/license/attribution bindings. Only the second proposal changes; selected evidence for the unchanged first donor must still succeed.

| New form | Sample rate | Frames | Interval | Duration | Expected result |
|---|---:|---:|---|---:|---|
| Lower boundaries | 8,000 | 1 | 0–1 | 0.000125 | Metadata accepted |
| Upper boundaries | 192,000 | 33,554,410 | 0–33,554,410 | frames / rate | Metadata accepted |
| Rate below minimum | 4,000 | 80 | 0–80 | 0.02 | Range refusal |
| Rate above maximum | 200,000 | 80 | 0–80 | 0.0004 | Range refusal |
| Zero frames | 8,000 | 0 | 0–0 | 0 | Range refusal |
| Frames above cap | 8,000 | 33,560,000 | 0–33,560,000 | 4,195 | Range refusal |
| Negative start | 8,000 | 80 | −1–79 | 0.01 | Range refusal |

Every negative form has exactly one invalid range category: rate, frame count or start. Zero frames necessarily also gives an empty interval when `end - start == frames`; this does not independently isolate `frames > 0` from `end > start`. No guard-deletion mutation test was run.

Both WAVs are removed before metadata reads. Each positive form must pass the selected-evidence, directory whole-packet and owned whole-packet APIs. This establishes declared metadata acceptance at the boundaries, not measured audio: no large WAV is allocated, and source annotations remain synthetic and unvalidated by these metadata APIs.

Every negative form must return `InvalidArgument` with exactly `Prepared donor declared PCM facts are inconsistent or exceed canonical WAV bounds` through five paths: selected directory evidence, full directory loading, owned snapshot validation, directory packet facts and owned packet facts. Full loading must therefore refuse before reporting the missing WAV. The owned snapshot retains its original small WAV; a later audio mismatch cannot satisfy the exact diagnostic assertion. Whole-packet refusals target the second proposal after the first has remained valid, covering the facts used by candidate verification without constructing a new signed candidate.

## Exact focused evidence

Base commit: `3df28db47b4d1403124a1565aa05623ce49336d8`. The only implementation/test change is the 88-line addition to `tests/test_prepared_phone_donor_snapshot.cpp`; patch SHA-256 `2a59cb1b50f2ddd492e7a79f39cb11fb7d90c29ce1a214026bf406a2b6f8f41b`.

- The Release snapshot target and its dependencies built successfully in **5.151 s**, including actual compilation and linking of the changed test. This was a focused build, not a full build.
- Its single CTest lane passed **22/22 native cases** in **0.967 s**. Two new cases contain two positive forms and five negative forms; seven traces do not mean seven new test cases.
- The tested binary is retained and its before/after size and SHA-256 agree. HEAD and the exact source diff stayed stable; the version header embeds the base commit. The runtime source/header pins match their base Git blobs.
- The frozen manifest under `out/evidence/prepared-donor-pcm-range-negatives-2026-10-15` binds **18 artifacts and 8 pins**, **5,452 bytes**, SHA-256 `72f93ac7eb907fac2f536ece9dd67aa0784c5c0ff5e4d484f9ff9fdbd385fe40`. The read-only verifier checks all identities, source snapshots, compilation/result logs and all seven exact traces without executing retained tests or native tools.

No Python, full suite, new sanitizer, leak, TSan, OS fault, power-loss, Studio, device, DAW, Windows, acoustic or human checks were run for this test-only unit. The originating combined review of `7e141c0f` and `24cf12d5` is retained and pinned; its approval of the runtime geometry repair is separate from the still-pending exact review of these new tests. The earlier Python test repair `3df28db4` has its own independent review in progress.

## Remaining acceptance work

The native range coverage P3 is locally repaired, pending exact independent closure. Wrapper cancellation/commit-fault coverage, the lower-impact style-migration follow-up and optional parser/CLI presentation work remain distinct. No downloaded or prepared real recording was modified, registered, imported, played or approved. Genuine source/performer disposition, producer and distinct reviewer, phone/pitch/style coverage, failed-QC policy and listening remain required.

Canonical Alpha remains **0/20**, U45 **HELD**, Beta **BLOCKED**, full goal **ACTIVE**. `.github` and README are untouched; Windows remains TODO.
