# UV-gated excitation: the level recovery is larger than the ledger's framing

Date: 2026-09-23. Read-only analysis of already-retained artifacts. No new
training, sampling, inference or measurement was run. Nothing here is an
approval, and neither arm is a qualified singer.

## What was checked

Retained paired experiment
`/Users/lhs/seam-corpus-pauses-2026-09-19-r1/vocoder-uvnoise-paired-r1/`
(`plan.json`, seed 929, arms `control` = `zero-v1` and `uvnoise` =
`uv-gated-v1`, one 2,804-update epoch each, identical dataset, parent
checkpoint, objective and budget). `run.json` in each arm binds
`reconstruction-000001` to `epoch-000001`.

Whole-source energy was measured from the retained WAV bytes with SciPy rather
than read from the JSON summaries, and the summaries were then checked against
those bytes.

## 1. The receipts describe the files they name

All 12 items in both arms: independently measured WAV RMS equals the receipt's
`renderedRms` exactly, **0 mismatches of 24**. Worth recording because the
uv-gated evidence that exists is read from summary fields; those fields are
faithful to the audio they name.

## 2. Whole-source level against the source each item names

| Arm | Mean | Min | Max | Spread |
|---|---:|---:|---:|---:|
| `control` (zero-v1) | **−7.29 dB** | −11.10 | −5.60 | 5.50 dB |
| `uvnoise` (uv-gated-v1) | **−2.85 dB** | −4.31 | −1.78 | 2.53 dB |

Paired by source, uvnoise is closer to source level on **12 of 12 items**, by
1.61 to 7.34 dB (mean **4.44 dB**), and the spread between songs narrows from
5.50 to 2.53 dB. Spectral distance improves in the same direction on the same
items (control 0.799–1.031, uvnoise 0.507–0.714).

## 3. The guardrail-relevant term is the voiced one, and it crosses into range

The level guardrail in the flatness ablation is `unvoicedRmsRatioRange` with
range `[0.8, 1.2]`. Against the paired per-phone measurement (27 unvoiced and
86 voiced windows over five frozen sources):

| Term | control | uvnoise | In `[0.8, 1.2]`? |
|---|---:|---:|---|
| unvoiced `meanRmsRatio` | 0.9651 | 0.8217 | both within; uvnoise closer to the lower edge |
| **voiced `meanRmsRatio`** | **0.6593** | **0.8885** | **control is OUTSIDE; uvnoise is inside** |

The control arm's voiced level ratio 0.6593 sits **below** the range. The
uv-gated arm moves it to 0.8885, which is inside. So on this measurement the
excitation change repairs a guardrail violation on the voiced term, while
leaving the unvoiced term inside the range but nearer its lower bound.

## Why this differs from the ledger's framing

`docs/implementation/INTEGRATED_SINGER_EXECUTION.md` reports the same raw
numbers ("Unvoiced RMS ratio moved 0.965 to 0.822; voiced RMS ratio moved 0.659
to 0.888") but characterizes the arm as "a modest measured improvement",
because it leads with the signed unvoiced residual: 0.226 → 0.203, an increment
of 0.024, which it notes is roughly a tenth of the periodicity objective's own
0.239 effect.

Both readings are of real quantities and neither is wrong. They answer
different questions, and the ledger's heading understates the level one:

- The 0.024 residual is a per-phone, hop-aligned **correlation** difference. It
  barely moves because the defect it measures is not primarily an energy
  problem.
- The 4.44 dB figure is **whole-source energy**, and it moves a lot.
- The voiced level ratio crossing from outside `[0.8, 1.2]` to inside is a
  guardrail result, which the "modest" framing does not surface.

The practical consequence: excitation is a cheap, already-implemented lever on
the level defect, and the ledger's summary makes it look like a dead end.

## What this does not show

- **Pitch does not improve at all.** Both arms report `pitchStatus FAIL` on all
  12 items. Level and pitch are separable, and this experiment says nothing
  about pitch, which remains the primary blocker.
- One training seed, one inference seed, five frozen sources in the paired
  comparison and 12 items in the reconstruction receipt. A replicated or
  statistically established benefit is still not claimed.
- Per-song regressions exist and are not cancelled by the aggregate: in the
  paired comparison `procedural-song-00024` pitch worsens 27.66 → 33.30 cents
  and unvoiced `|rmsRatio-1|` worsens 0.172 → 0.189.
- −2.85 dB mean is an improvement, not parity. Even the uv-gated arm is ~2.9 dB
  below source on average.
- The unvoiced term does get slightly worse (0.9651 → 0.8217). It stays inside
  the declared range, but a change that trades one term against another should
  be decided, not assumed.
- Every limit already recorded still applies: `labelOrigin` is
  `com.project-seam.training-generated-teacher`, `trainingAdmitted`,
  `singerQualified` and `releaseEligible` are all false, the corpus is one
  synthetic recipe, and no listener has heard either arm.

## Next step this suggests

Promote `uv-gated-v1` only after re-running the frozen guardrail evaluation
with excitation enabled and confirming the voiced and unvoiced ratios both land
inside `[0.8, 1.2]` and that pitch does not regress. The measurement above is
evidence that the attempt is worth making, not evidence that it succeeds.

