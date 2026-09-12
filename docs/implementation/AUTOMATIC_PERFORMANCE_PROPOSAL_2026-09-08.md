# Automatic performance proposal foundation

The synthesis layer now exposes `generateAutomaticPerformance(...)` as a
side-effect-free proposal producer. A request captures the region identity,
performance revision, resource identity, pronunciation identity, generator
identity, seed and bounded time range. The generator resolves no new mutable
state and returns a `Proposed` `PerformanceTake` with deterministic Pitch,
Dynamics, Attack and Release lanes. Repeating the same request yields byte-
equivalent typed data; changing the seed changes only the generated pitch
offsets within the validated MIDI-cent range.

The proposal is delivered through the existing `EditorSession` performance-job
context and `AddPerformanceProposalCommand`; acceptance remains a separate
`SetAcceptedPerformanceCommand`. Stale revision, pronunciation, range,
duplicate-channel, cancellation and unsupported advanced-channel requests
fail before any document mutation. Rejected proposals therefore remain
inspectable metadata rather than silently becoming canonical performance.

This is a deterministic contract backend and lifecycle proof for U38. It is
not a trained neural singer, acoustic-quality evidence, or a claim that the
advanced timbre channels are implemented. U36/U37/U39/U40 still own qualified
models, advanced controls, native take comparison and independent creator
acceptance.

