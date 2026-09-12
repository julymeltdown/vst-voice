# Candidate pitch inspection v1

This is an unsigned, export-only technical estimate record. It is not a bank, a producer review decision, a rights attestation or an approved QC packet. No import/admission path is defined by this format.

Studio exports it with Cmd/Ctrl+E after N-key inspection. The native save action rechecks workspace epoch, generation, row and inspected key after the dialog. The controller also requires matching current audio digest, boundary revision, key and span. A new-file-only atomic write refuses any existing destination, even if the native panel offered an overwrite confirmation. Maximum serialized size is 1 MiB. Export does not save a producer generation or modify review flags.

## Fields

- `formatId`: `com.project-seam.candidate-pitch-inspection`; integer `schemaVersion`: 1.
- `status`: `ESTIMATE_ONLY`; `approved`: false; `exportedAtUtc`: validated UTC timestamp.
- Captured source context: `projectId`, `takeId`, `inventorySha256`, `operatorId`, `sourceGeneration` (decimal string), `audioSha256`, `recipeHash`, `boundaryRevisionId`, `phonemeKey`.
- Scope: integer `startFrame`, exclusive `endFrame`, `sampleRate`, and inventory `targetMidi`. Frames are coordinates within the raw WAV, not within the song timeline or a derived audio revision.
- Analyzer identity: `analyzer` = `seam.normalized-autocorrelation.v1`, `correlationMethod` = `fft` or `direct`, `frameSize`, `hopSize`, `minimumHz`, `maximumHz`, `voicingThreshold`.
- Summaries: `medianHz` and `centsFromTarget`, or null when no voiced estimate exists. Cents are relative to the inventory target, not independently validated score compliance.
- `frames`: objects containing raw-WAV window-start `sourceFrame`, `voiced`, nullable `f0Hz` and numeric `confidence`. Unvoiced windows export null F0, not an invented frequency. The plotted window center is `sourceFrame + frameSize / 2`. Confidence is the estimator's correlation score, not a calibrated probability.

The captured generation can be older than a later non-musical save; it identifies the source of the analysis, not a freshness or authorization claim. Original audio, recipe and producer metadata are not bundled. Reproduction requires the exact corresponding raw PCM and declared estimator/settings. Hashes bind referenced bytes but do not authenticate the exporter or establish ownership/rights. A downstream QC gate must independently validate source availability, method/version, scope, reviewer authority and acceptance criteria.

## Verification and limits

Strict Debug/Release native Studio and integration builds pass. All 18 integration cases pass (2.67/0.95 seconds), including actual report parsing, identity/frame/method checks, invalid timestamp rejection, overwrite preservation, stale inspection rejection and unchanged producer state. `git diff --check` passes. Native save-panel interaction, Windows execution, downstream report validation/admission and acoustic/listening qualification remain unverified. Existing inspection and producer files are not silently upgraded or approved by this export.
