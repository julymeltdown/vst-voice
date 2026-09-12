# SEAM bounded Standard MIDI File interchange v1

This document describes the implemented codec boundary in
`libs/seam-interchange`. It is an inert score conversion format: a MIDI file
never supplies an executable, voicebank, dictionary, model or path to the
renderer.

## Supported input

- SMF Type 0 (one track) and Type 1 (one or more tracks), PPQ division only.
- Note-on/note-off events, including velocity-zero note-off and running status.
- Set-tempo (`0x51`), time-signature (`0x58`), lyric (`0x05`) and text (`0x01`)
  meta events.
- Overlapping notes with the same channel/key are paired FIFO, so note identity
  is deterministic even when a file contains repeated overlapping keys.

SMPTE divisions, malformed status/data bytes, invalid VLQs, truncated chunks,
zero tempo, invalid meter and trailing bytes are rejected. Unsupported channel,
system, SysEx and meta events are skipped only with a bounded `SmfIssue` loss
record; they are never silently presented as preserved score data. A dangling
note is closed at the track's final tick and receives an explicit warning.

## Limits and safety

`SmfLimits` caps the input bytes, tracks, events, notes, UTF-8 text expansion
and absolute ticks. Chunk lengths are checked against the held input span before
any track payload is constructed. Variable-length values are limited to four
bytes and `0x0fffffff`. Text must be valid UTF-8 and NUL-free. The parser does
not resolve external paths or allocate from file-declared sizes without first
checking them against the limits.

## Deterministic output

`encodeSmf` emits a Type-1, single-track PPQ file. Meta events are ordered before
note events at equal ticks; note-offs precede note-ons; all equal-time ties use
stable byte ordering. The score is validated before output construction and the
original score is not mutated. Output size and four-byte track length are
bounded. Import/export lifecycle, native conversion review and external-DAW
interoperability remain separate U29/U30/U32 acceptance work.
