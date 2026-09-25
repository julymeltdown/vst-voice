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
- Track-name (`0x03`) events and source-track membership for notes and text.
- Overlapping notes with the same channel/key are paired FIFO, so note identity
  is deterministic even when a file contains repeated overlapping keys.

SMPTE divisions, malformed status/data bytes, invalid VLQs, truncated chunks,
zero tempo, invalid meter and trailing bytes are rejected. Unsupported channel,
system, SysEx and meta events are skipped only with a bounded `SmfIssue` loss
record; they are never silently presented as preserved score data. A dangling
note is closed at the track's final tick and receives an explicit warning.

## Limits and safety

`SmfLimits` caps the input bytes, tracks, source events, canonical serialized
events, notes, UTF-8 text expansion and absolute ticks. `maximumEvents` bounds
events consumed from an input file and diagnostic amplification;
`maximumSerializedEvents` independently bounds encoded score events. The latter
allows a bounded missing-note-off repair to add one canonical note-off per
admitted open note without misclassifying that synthesized event as source
input. Chunk lengths are checked against the held input span before
any track payload is constructed. The note ceiling is enforced against both
completed notes and currently active note-ons before retaining each note in a
per-key queue, so unmatched note-ons cannot consume the larger event budget.
Variable-length values are limited to four bytes and `0x0fffffff`. Retained
text is valid UTF-8 and NUL-free; malformed annotation/lyric payloads are
discarded with explicit losses. The parser does not resolve external paths or
allocate from file-declared sizes without first checking them against the
limits.

Track names use the same cumulative byte budget. A name that is not NUL-free
UTF-8 is discarded with a loss diagnostic rather than rejecting otherwise
valid musical events; the importer does not guess legacy code pages.
Text and lyric payloads also consume the cumulative raw-byte budget. Invalid
UTF-8 or embedded-NUL annotations/lyrics are discarded with a loss diagnostic
while notes remain importable. One trailing NUL is stripped from lyric payloads
as a compatibility terminator and reported as a warning, unless it leaves an
empty lyric; empty lyric payloads are discarded with a loss. SEAM does not guess
Shift-JIS or other legacy encodings. Repeated malformed-text diagnostics are
aggregated per track and category with a source-tick range and PPQ. This v1 maps vocal
lyrics only from `0x05`; lyric text carried in ordinary `0x01` events is retained
by the codec as annotation text but is not assigned to vocal notes by project
import, so Soft Karaoke-style `0x01` lyric mapping is not supported.

## Deterministic output

`decodeSmf` retains source track order and names, including empty or
conductor-only tracks. Note and text records retain their source track index;
tempo and meter remain score-global. Project import creates a separate SEAM
vocal track and region for each source track containing notes and matches lyric
events within that source track. When lyric events are placed on a note-free
track, they are paired by tick only if there is exactly one note-bearing track,
after all same-track lyric assignments have been made, with a warning;
ambiguous or duplicate lyrics remain explicit losses. Empty source tracks remain
in the interchange score and do not create empty singer tracks.
Note-bearing tracks with no matched lyrics, and tracks containing MIDI channel
10 percussion, are imported without dropping notes but are flagged for review
because both are mapped to vocal notes.

`encodeSmf` emits deterministic Type-1 PPQ output with the retained number of
tracks (or one default track for a newly constructed score). Global tempo/meter
events are emitted on track 0; notes, text, lyrics and names remain on their
assigned tracks. Meta events are ordered before note events at equal ticks;
note-offs precede note-ons; same-priority events retain deterministic source
insertion order. The track-name event is first at tick zero. The score is
validated before output construction and the original score is not mutated.
Project-to-SMF conversion exports every vocal track as one named Type-1 MIDI
track by default, combining that track's regions at their absolute project
timeline positions. The lower-level API also supports explicit single-region
export; the service rejects a partial track/region selection rather than
silently choosing a default. Audio tracks are omitted with an explicit loss.
SEAM-only note, region, track and performance controls are disclosed as losses.
An invalid or over-budget project track name is omitted with a loss while
musical events remain exportable. A lyric containing NUL or exceeding the
cumulative text budget is omitted with an explicit loss while its note remains
exportable. Output size and four-byte track lengths are bounded. Native
conversion review, multi-track GUI verification and external-DAW
interoperability remain separate U31/U32 acceptance work.
