# Phoneme timing display

The lower band shows nucleus spans derived from the shared timing compiler. Editing a nucleus moves its displayed anchor and the preceding automatic end together. Explicit boundaries use note-relative microsecond offsets, as in rendering.

The upper band holds onset, geminate and coda labels. A `~` prefix means part of that span is still an estimate: an unedited source-dependent extent is displayed as a fixed-width label, not a measured duration. Providing an explicit boundary replaces that estimate for the boundary; it does not invent missing voicebank landmarks.

A `!` prefix marks a timing conflict. The token remains visible for editing, but its fallback layout is not a valid rendering plan. Rendering reports the actual timing conflict; it does not silently accept the fallback geometry.

Painting and phoneme hit testing share absolute editor coordinates. The separate unit row uses horizontal coverage without inheriting the phoneme bands. The display currently compiles at a 48 kHz reference rate and converts to timeline pixels; actual rendering rounds timing at its requested output rate.

These symbols describe timing certainty, not voicebank availability or singing quality. Invalid/conflicting edits remain saved and can be corrected or undone.

## Release bounds

An explicit phoneme end may extend past its note, but must remain within the owning region. An end beyond the region is a conflict: shorten its offset or extend the region. The saved value is not silently clamped. Distinct overlapping notes retain independent timing; the within-note ordering rule does not force separate notes into a single sequential voice. Negative source preutterance remains allowed and is clipped at project frame zero without shifting surviving audio.

Moving a consonant start earlier does not create more time after its vowel. The selected unit must still have enough vowel-to-end time for its transition into the stable region. Conversely, shortening the consonant does not consume that post-vowel budget. Invalid combinations report a conflict rather than extending the target end.

## Seam overlap

The seam overlap setting limits the crossfade inside the actual audio overlap. It does not move either unit or shorten the incoming unit. Once the window ends, incoming samples replace overlapping outgoing samples; zero overlap means an immediate handoff. Phase-continuity processing may choose a phase basis for the incoming waveform, but it does not change the unit's target start or duration. These sample-composition semantics are separate from whether the join sounds musically convincing.
