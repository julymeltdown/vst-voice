# Renderer capability and cache provenance

## Implemented boundary

`RendererControlRequest` is an explicit render contract for pitch, timing,
dynamics, vibrato, attack, release and the advanced timbre controls. The
dispatcher validates required controls before it opens source audio or enters
a backend. A required unsupported control returns `Unsupported`; it is never
silently erased by the legacy Raw fallback. A source-aligned transient that
requires pitch preservation also prevents a Raw fallback.

The Raw backend now consumes `PitchCurve` directly. It integrates a bounded
per-sample sustain trajectory when no compiled performance is present, while a
compiled performance remains authoritative and cannot be combined with a
second curve. The curve is included in the snapshot identity and the Raw
renderer revision is incremented so old cache records cannot be reused for the
new trajectory.

## Cache record

PCM cache format revision 4 stores the actual renderer identity, fallback
count and first fallback diagnostic beside the PCM. Disk hits restore these
fields before publication; region results therefore report the same fallback
provenance on cold and cached renders. Renderer identity text and diagnostic
sizes are bounded before allocation. Older cache revisions are rejected by the
existing exact-version check and are regenerated safely.

## Remaining qualification

The capability matrix describes the currently implemented DSP path. It does
not claim that advanced controls (formant, breathiness, tension, airiness,
gender, style blend or growl) are implemented by the current classical/Raw
backends; requests for those controls fail until U39 supplies a qualified
resource/backend combination. Acoustic and listener qualification of every
renderer, plus installed-host evidence, remains a later Beta GO gate.

