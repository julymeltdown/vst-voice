# Voice recipe v4 — plosive source bindings

Schema 4 adds a nonempty `plosives` array to the schema-3 root shape. `frications`
remains present and may be empty; each voiced pose retains its nullable `nasal`
field. Without plosive bindings, canonical serialization remains schema 1, 2 or
3 exactly as before. Schema-4 input with an empty plosive array rejects rather
than silently downgrading.

Each plosive binding has exactly:

| Field | Contract |
| --- | --- |
| phone | One of the initial unvoiced stop identifiers `p`, `t`, `k` |
| style | Existing voiced-pose style; nonempty bounded UTF-8 |
| seed | Canonical unsigned-64-bit decimal string |
| centerHz | Finite 80–16,000 Hz |
| bandwidthHz | Finite 20–16,000 Hz; center/bandwidth Q 0.25–20 |
| gain | Finite linear 0–0.25 |
| burstMilliseconds | Finite nominal burst duration, 1–100 ms |

At most 64 bindings are allowed. `(phone, style)` must be unique and cannot also
appear as a frication binding: there is no implicit choice between a sustained
noise source and a stop burst. Existing JSON byte/depth/node/collection limits
remain active. Unknown/missing fields, numeric seeds, invalid styles and
non-finite/out-of-range values reject.

Frozen resource version is `4`; the exact canonical JSON hash includes every
binding and duration. Decode requires payload/version/ID agreement. Edits create
new immutable identities. Removing the bindings deliberately returns to the
appropriate earlier schema, preserving those schemas' prior canonical encoding.

## Integration contract

The configured burst duration is nominal milliseconds, independent of output
sample rate. Articulation must convert it at the actual rate and validate that
the available onset has room for both closure and burst; it must not silently
reinterpret the binding as sustained frication. Source-specific Nyquist and
gesture bounds are validated by the DSP path.

The subsequent [candidate-v4 integration](PROCEDURAL_CANDIDATE_V4.md) connects
these bindings to timed unvoiced onsets, closure/burst rendering, and candidate
bake/import. Native creation/edit and source-audition controls are now wired;
live native interaction qualification remains open. A stored `k`
binding still requires compatible timing and a vowel pose to render `ka`.
No source rights, intelligibility or musical approval follows from this schema.
