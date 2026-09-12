# Voice Designer sustained-pose audition

## Implemented path

`renderDesignerAudition(resource, poseIndex, midiKey, stopToken)` renders one second of mono Float32 preview at 48 kHz using the existing compiled-F0 phonation source and oral vocal-tract stage. It decodes the immutable recipe, validates the selected pose and MIDI 36–96 range, and retains backend rejection of unsupported configurations. The preview uses a synthetic fixed-pitch score only; no user song is modified.

Output must be finite. Attenuation limits peak to 0.9 without boosting quiet audio; short edge ramps start/end at zero. The session renders on a background worker with captured voice epoch/revision and owns cancellation/join. Edits, undo/redo and voice replacement invalidate prior audio; obsolete or cancelled completions are not adopted. Previously copied audio buffers remain immutable. File operations and audition have separate workers; saves do not mutate a running render's frozen input.

In Studio Designer, Space first requests rendering of the selected pose and MIDI pitch (initially pose 0/MIDI 69). The control list now includes audition pose/style and pitch: Up/Down/Tab selects the row, Left/Right cycles the recipe's poses or adjusts pitch within MIDI 36–96. Once `POSE READY / SPACE PLAY` appears, Space starts the existing audition device session at gain 0.25. Space during playback stops it; other Designer keys stop playback before acting. Escape cancels pending preview generation. Device creation/start occurs in the key handler, not paint. Paint only polls workers/device state and updates status. This is intentionally a two-action render/play interaction; continuous auto-audition is not yet implemented.

Audition selection is session-only state; it does not change the recipe revision/hash or undo history. Selection requires the current document epoch/revision. A changed pose/pitch cancels pending work and clears ready audio; an unchanged selection preserves it, and invalid selections preserve the last valid state. Completion admission compares pose/pitch as well as document identity. Create/Open resets selection to pose 0/MIDI 69; an edit/undo/redo that removes the selected index returns it to pose 0.

## Verification

### Dedicated frication preview

The Designer now renders selected frication sources directly through `FricationSource`, using the same one-second/48-kHz, finite-value, attenuation-only peak bound and edge-ramp finalization as vowel previews. Select a frication parameter row and press Cmd/Ctrl+Space to render; press it again when ready to play at gain 0.25. Separate semantic Render/Play frication actions target the source index explicitly. This is a noise-source audition, not a voiced consonant, phonemized phrase or intelligibility claim.

Frication and vowel previews use separate ready buffers but share the single bounded audition worker. Completion retains document/revision/selection guards; source edits invalidate old buffers and cancelled work is not adopted. Vowel A/B pinning remains tied only to vowel audio and cannot accidentally capture a noise preview. Semantic vowel/reference playback dispatch avoids the primary modifier used by the new noise shortcut.

Added tests verify frame bounds, exact repeated noise PCM, finite/peak/edge/non-silent output, invalid source rejection, preservation of ready vowel/reference buffers, source-seed-dependent noise, stale worker suppression and shutdown cancellation. All twenty-one Designer tests and both Studio builds pass in Debug/Release. New frication playback interaction and acoustic qualification remain live-unverified; noise A/B and automatic continuous rendering are not implemented.

### A/B reference comparison

Cmd/Ctrl+B pins the currently ready preview as reference A, including immutable recipe resource, PCM, phone/style and MIDI pitch. Pinning rejects while work is pending, without ready audio, or for an obsolete document revision. Subsequent edits/undo/selection changes invalidate current B but preserve A. Opening or creating another voice clears the session reference; it is not written into the saved recipe or producer project. Cmd/Ctrl+Shift+B clears A without changing recipe history.

Space plays current B after rendering; Shift+Space plays pinned A. Reference playback requires the current pose phone/style and pitch to match the reference. A visible label identifies its pose/style/pitch and recipe-hash prefix. Both playback paths use the same 0.25 device gain, but this is not perceptual loudness matching: the existing per-render peak attenuation remains. Different timbres may have different perceived loudness. The reference is an editing comparison, not approval or listening-study evidence.

Added tests prove original recipe/PCM retention across edits, changed B audio, rejection without current audio, selection mismatch detection, reset on new voice, retained buffer lifetime and history-neutral clear. A/B hardware/UI interaction remains unverified; continuous comparison playback and broader listening qualification remain unfinished.

New tests verify exact repeat PCM, 48,000-frame dimensions, finite/peak bounds, zero edges, non-silence, changed PCM after an aspiration edit, invalid pitch/pose and pre-cancel rejection. Worker tests verify editing during rendering, stale-result suppression, subsequent successful adoption, immutable retained buffers and shutdown cancellation. All ten Designer tests and Studio builds pass in strict Debug/Release (0.77/0.56 seconds); diff checks pass.

No physical speaker/headphone playback or listening evaluation was performed at this checkpoint. The new Space/device handoff is wired but not yet exercised live. This is sustained-pose timbre preview, not a phonemized phrase/intelligibility test, female-voice qualification, generated take, approved bank or source-rights evidence. Pose/pitch selectors, A/B comparison, continuous slider audition and full U22/Beta GO remain unfinished.

Follow-up selection verification: an additional test switches pose/style/pitch during a worker, requires old-result suppression, compares accepted PCM against the exact selected render, verifies no-op/invalid selection preservation, different-pitch PCM and safe selection after pose removal. All eleven Designer tests and both Studio builds pass in Debug/Release (1.00/0.53 seconds); diff checks pass. Pose/pitch selectors are now implemented but their live UI/physical playback qualification remains open; A/B and continuous slider audition remain unimplemented.
