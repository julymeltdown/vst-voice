# Voice recipe v3 — explicit nasal coloration

Schema 3 extends the declarative `com.project-seam.voice-recipe` contract with an
explicit nasal model per pose. The engine identifier remains
`seam.source-filter.v1`; schema/resource and renderer revisions are separate.
No model, recipe or successful render grants rights or musical qualification.

## Canonical representation

Root fields are exactly the schema-2 root fields, including `frications`, which
may be empty in schema 3. Every pose contains `phone`, `style`, `nasalCoupling`,
`formants`, and `nasal`. `nasal` is either null or an object with exactly:

| Field | Unit | Finite bounds |
| --- | --- | --- |
| resonanceHz | Hz | 50–4,000 |
| resonanceBandwidthHz | nominal Hz, Q = frequency / bandwidth | 10–5,000 |
| antiresonanceHz | Hz | 50–16,000 |
| antiresonanceBandwidthHz | nominal Hz, Q = frequency / bandwidth | 10–5,000 |

Existing limits on poses, oral bands, frication data, UTF-8, JSON bytes/depth/nodes
remain in force. DSP admission additionally requires both nasal frequencies
strictly below the actual sample-rate Nyquist limit and strictly stable poles.
No frequency is silently retuned.

At least one pose must contain a non-null nasal model to encode schema 3. Without
one, the writer emits the original canonical schema 1 or 2, preserving old bytes
and resource identities. A schema-3 document with no model rejects rather than
silently downgrading. Legacy nonzero `nasalCoupling` without an explicit model
remains persistable but unsupported for rendering; it is not reinterpreted.

Frozen resources use version `3` for schema 3. All four model parameters and
coupling participate in the canonical hash. Decode requires agreement among
payload, ID, schema and resource version. Old frozen resources remain immutable.

## DSP contract

Let O be the existing normalized parallel oral-bank output, X the excitation,
R a constant-peak nasal band-pass, N a notch, and c the pose's nasal coupling:

```text
wet = N(0.5 * O + 0.5 * R(X))
output = (1 - c) * O + c * wet
```

The resonance and notch use the normalized RBJ biquad equations in the
[W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/).
The conceptual distinction between nasal resonances and spectral dips follows
the [Praat KlattGrid description](https://www.fon.hum.uva.nl/praat/manual/KlattGrid.html).
This SEAM topology and its 50/50 branch mixture are engineering choices, not an
implementation of KlattGrid or a calibrated physical airway model.

Zero coupling bypasses the new stage exactly. Nonzero coupling mixes complete
stable banks. Pose transitions crossfade independent source/target states rather
than interpolating poles. Block failure or cancellation advances neither state;
checkpoints and reset include resonance/notch memories. Finite-state and existing
output-magnitude checks remain enforced.

Sustained renderer revision is 9 (revision 7 introduced nasal coloration;
revision 8 corrected multilingual vowel routing; revision 9 adds the closed oral
path for nasal consonant poses); articulated stream revision is 4, including
standalone syllabic-N support. Existing
snapshot identities include those revisions, preventing reuse of older PCM under
the new renderer identity. Vowel and frication gesture labels retain their prior
meaning. Explicit `m`, `n`, `ng`, and `N` poses now select the closed-oral nasal
path described in [candidate v3](PROCEDURAL_CANDIDATE_V3.md). That path requires
positive coupling and an explicit model. Closures and stop bursts remain unsupported.

## Native editing

Voice Designer appends five controls: coupling, nasal resonance frequency/width,
and nasal antiresonance frequency/width. Editing explicitly enables a model whose
initial values are 250/90/1000/120 Hz. These are starting parameters, not a
qualified female-singer preset. Coupling starts at the stored value, normally
zero. Undo can restore the original schema-1/2 resource; lowering coupling to zero
retains explicit schema-3 model data without changing oral audio.
