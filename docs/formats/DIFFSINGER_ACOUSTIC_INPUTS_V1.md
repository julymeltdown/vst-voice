# DiffSinger acoustic input preparation

Status: implemented common input conversion; no ONNX execution or generated PCM.
This advances U37's selected backend integration and does not complete U35/U36/U37.

## Inspected source

OpenVPI DiffSinger revision `336cf01b57f2ad44c6b37a79cf33993043291759` was cloned
into ignored development storage and inspected without executing its scripts.
The Git archive SHA-256 is
`034a9e612b2320f35ae7018cd5bf34ae24ce82c69aeb2310291312bd3db637ef`.
The reference is recorded in `third_party/manifest.yml`; upstream code, models
and vocoders are not incorporated in the shipping dependency set by this change.

Relevant upstream implementation:

- [Acoustic exporter](https://github.com/openvpi/DiffSinger/blob/336cf01b57f2ad44c6b37a79cf33993043291759/deployment/exporters/acoustic_exporter.py): token/duration/F0 input shapes, configuration-dependent extra inputs, and `steps` for the inspected decoder export.
- [FastSpeech2 ONNX implementation](https://github.com/openvpi/DiffSinger/blob/336cf01b57f2ad44c6b37a79cf33993043291759/deployment/modules/fastspeech2.py): token-zero masking before length regulation.
- [Vocoder exporter](https://github.com/openvpi/DiffSinger/blob/336cf01b57f2ad44c6b37a79cf33993043291759/deployment/exporters/nsf_hifigan_exporter.py): separate mel/F0-to-waveform graph.

The repository's older acoustic benchmark mentions `speedup`; this implementation
uses the inspected exporter's `steps` contract. Neither name may be assumed for
an arbitrary downloaded model. The loaded graph must be inspected and matched.

## Conversion policy

`prepareDiffSingerAcousticInputs` accepts a validated `NeuralRequest`, its
`ModelContract`, verified `NeuralVocabulary`, and an explicit 1–1000 step budget.
It emits batch-one data ready for the common acoustic tensor bindings:

| Field | Tensor type/shape | Meaning |
| --- | --- | --- |
| `tokens` | int64 `[1, N]` | Exact vocabulary IDs; ID 0 is rejected because it is padding in the inspected graph. |
| `durations` | int64 `[1, N]` | Positive durations in acoustic hop frames. |
| `f0Hz` | float32 `[1, T]` | Existing compiled pitch sampled on the acoustic grid. |
| `steps` | int64 scalar | Explicit bounded inference iteration count. |

For hop H and sample count S, T is `ceil(S/H)`. Internal cumulative phone
boundaries are rounded to the nearest hop (ties up); the final boundary is T.
Durations are differences of those cumulative boundaries, avoiding cumulative
rounding drift. A boundary that would erase a phone is an actionable Unsupported
result. Tokens are neither merged nor dropped to force a successful conversion.
Silence is an explicit nonzero vocabulary entry, independent of padding.

Each acoustic frame samples F0 at its hop center, clamped into its owning input
phone span. This preserves silence/unvoiced F0 at rounded boundaries and does not
apply pitch or vibrato a second time. The result preserves the requested output
sample count and padded sample count so a future vocoder owner can trim exactly.
Sample-domain dynamics remain in the request for output gain; they are not
silently reinterpreted as a learned acoustic energy/variance input.

Cancellation and request/model/vocabulary/shape validation precede tensor
allocation; cancellation is also checked during token and F0 preparation.

## Verification and remaining work

Tests cover cumulative duration conservation across nonaligned spans, final-hop
padding, ownership at half-hop boundaries, voiced/unvoiced F0, token-zero rejection,
sub-hop phone erasure, missing conditioning, incorrect vocabulary counts, invalid
F0, invalid step budgets, and cancellation. Release build and neural/core suites
passed 2/2 in 23.16 seconds.

Actual inference still requires a verified acoustic/vocoder model bundle, native
ONNX runtime integration, graph/operator and input/output schema validation,
configuration-specific speaker/language/variance conditioning, mel compatibility,
and waveform validation/trimming/gain application. Those are real implementation
gaps. No trained singer, reproduced training run, inference throughput, acoustic
quality or installed-runtime acceptance is established by these tensor tests.
