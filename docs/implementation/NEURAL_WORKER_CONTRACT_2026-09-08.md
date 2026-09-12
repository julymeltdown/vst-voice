# U37 — Bounded neural worker contract (2026-09-08)

## Implemented boundary

`libs/seam-neural-synthesis` defines a versioned `SNW1` frame. Every frame has
an explicit little-endian header, bounded JSON metadata, and a bounded
Float32 payload. Request metadata binds request ID, model ID/version/content
hash, pronunciation hash, sample rate, channel count, frame count, and the
feature schema. Responses bind request ID, model hash, backend ID, output
shape, and bounded PCM.

The decoder checks the magic/version/type, exact frame lengths, metadata
schema, finite numeric values, model hashes, shape and frame limits before
allocating arrays. The model contract independently binds model and vocabulary
hashes, hop size, sample rate and output shape. `runNeuralWorker` launches only
an explicitly supplied absolute helper through the existing no-shell,
bounded-I/O, cancellable process runner and rejects stale/mismatched output.

The helper request now has an explicit bounded stdin ceiling. The historical
4 KiB default remains unchanged for dictionary helpers; neural callers opt into
the larger framed limit explicitly.

## Evidence

`seam_neural_worker_protocol_tests` covers request/response round trips,
malformed headers and lengths, non-finite values, metadata/key rejection,
model-contract mismatch and response-shape/range validation. The real
`seam_neural_worker_probe` is executed through the bounded helper runner, so
the IPC path is exercised rather than only testing byte functions.

## Deliberate boundary

This change does not claim a trained model, ONNX Runtime integration, model or
vocoder rights, acoustic qualification, package materialization, or installed
standalone/CLAP/VST3/AUv2 deployment. The probe emits deterministic silence and
is test infrastructure only; it is not a singer resource. U35/U36 must provide
lawful reproducible data and a qualified model before this adapter can be
connected to production rendering or Beta-GO evidence.
