# Acoustic Analysis v1

formatId: com.project-seam.acoustic-analysis, schemaVersion: 1.

A stored, versioned measurement of one unit's audio: where it is voiced, what
fundamental the voiced parts have, and how confident the analyser was. The point
of storing it is that a conclusion derived from audio has to stay attached to the
audio it came from, and to the algorithm that reached it.

```json
{
  "formatId": "com.project-seam.acoustic-analysis",
  "schemaVersion": 1,
  "unitId": "ja.original.a4.k-a.01",
  "audioSha256": "<64 lowercase hex>",
  "sampleRate": 48000,
  "decodedFrames": 24255,
  "algorithmId": "seam.pitch.fft-autocorrelation",
  "algorithmVersion": "1",
  "spans": [
    { "start": 0, "end": 18688, "voiced": true,  "f0Hz": 200.0, "confidence": 0.98 },
    { "start": 18688, "end": 27392, "voiced": false, "f0Hz": 0.0, "confidence": 0.095 },
    { "start": 27392, "end": 48000, "voiced": true,  "f0Hz": 240.0, "confidence": 0.978 }
  ]
}
```

## Location

A unit's sidecar lives at analysis/<sha256-of-unit-id-bytes>.json relative to the
bank root. Keying by digest rather than by ID means an ID is never interpreted as
a path component, and two units sharing one WAV have distinct analyses. This
matches the alignments/ convention; see SOURCE_PHONEME_ALIGNMENT_V1.md.

Absence is meaningful: it means no measurement has been stored, not that the audio
is unvoiced.

## Spans

Spans partition [0, decodedFrames) exactly: each begins where the previous ended,
the first starts at 0, and the last ends at decodedFrames. A gap or an overlap is
rejected, because either one leaves some sample described twice or not at all.

Spans are not analysis frames. Frames overlap (a frame at origin s covers
[s, s + frameSize) while frames are one hop apart), so a sample is covered by up to
frameSize / hopSize frames. Emitting one span per frame would make the same audio
described several times with possibly different answers. Each frame is instead
given the region no later frame also covers, and consecutive frames that agree are
merged, so a span means "the analysis found one state throughout".

f0Hz is the median fundamental over the voiced frames inside the span, or 0.0 for
an unvoiced span. An unvoiced span reporting a fundamental, or a voiced span
reporting none, is rejected as self-contradictory: either field alone would then
mislead a consumer that trusts it.

confidence is the mean analysis confidence over the span, in [0, 1]. A low value
means the analyser was unsure. These are measurements, not labels: a
low-confidence span is a proposal for review and does not establish phonetic
correctness or a reviewer judgement.

Bounds: maximumSpans defaults to 4096. A source that alternates more often than
the budget allows is refused rather than truncated, because a truncated analysis
would describe part of the audio while appearing complete.

## Algorithm identity

algorithmId and algorithmVersion name the implementation that produced the record.
Both are written on encode and both are checked on decode and on validate. A
record from a different revision is refused so that the derivatives must be
regenerated, rather than being silently reinterpreted by a newer analyser. Bump
kAcousticAnalysisAlgorithmVersion in acoustic_analysis.hpp whenever the analyser
answers could change.

The configuration the analysis runs under is single-sourced as
producerPitchConfig(), producerPitchMarkConfig(), producerAnalysisWork() and
producerPitchLimits() in the same header. The producer draft path, the bank QC and
any installed-bank re-measurement use those, so "the analysis" means one thing.
Earlier, three sites each wrote their own constants and agreed only by convention;
that is how the three resamplers in this repository drifted apart.

## Bounds

- One sidecar: at most 512 KiB encoded and decoded.
- unitId at most 1024 bytes; digests exactly 64 lowercase hex characters.
- sampleRate within [8000, 384000]; decodedFrames must equal the decoded audio
  extent exactly.
- Parse limits: depth 4, 8192 nodes, 4096 collection entries.

## Validation

validateAcousticAnalysis checks unit identity, digest match against the verified
audio, decoded extent, span ordering and coverage, finite in-range values, the
voiced/unvoiced fundamental rule, and algorithm identity. decodeAcousticAnalysis
is a boundary and validates every field before returning, so a hand-edited or
hostile sidecar fails rather than half-loading.

## What this does not establish

Structural validation does not establish that the voicing decision is correct,
that the fundamental is accurate, or that a human has reviewed either. Those
require listening and independent judgement.

