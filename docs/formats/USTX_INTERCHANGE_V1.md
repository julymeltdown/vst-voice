# Native USTX interchange subset v1

Project SEAM now has a native, bounded USTX 0.9 boundary. This is an actual
codec and conversion path, not the earlier creator-study Python bridge.

## Accepted input

- UTF-8 USTX 0.9 YAML with one mapping document.
- Block mappings/sequences and flow mappings/sequences used by OpenUtau's
  ordinary `tracks`, `voice_parts`, `notes`, `tempos`, and
  `time_signatures` records.
- Tempo positions and note/part positions use USTX's 480 PPQ grid.
- Note pitch points use OpenUtau's millisecond `x` offset and tenth-semitone
  `y` offset. `l`, `sp`, and the common smooth shapes are mapped to SEAM
  linear, step, and smooth interpolation.
- Note vibrato is mapped from USTX percentage/frequency fields into the
  persisted SEAM vibrato contract with bounded clamping diagnostics.

The reader rejects aliases/anchors/tags, multiple documents, duplicate keys,
tabs in indentation, malformed flow syntax, non-finite numbers, invalid UTF-8,
and any input or collection that exceeds `UstxLimits`. Limits are checked while
parsing, before typed arrays are grown from declared values.

## Conversion semantics

Import creates a new unsaved `domain::Project` draft. It scales 480 PPQ to the
canonical 960 PPQ model, retains tempo/meter, track mix fields, notes, lyrics,
pitch automation and vibrato, and carries an explicit bounded loss/warning
list. Singer references, voice colors, renderer/phoneme details, part curves,
SEAM ownership/units, and other unsupported controls never become executable
state by accident.

Export is deterministic and emits a USTX 0.9 subset. Project ticks are rounded
to 480 PPQ with warnings; millisecond pitch positions are derived from the
authoritative tempo map. Unsupported SEAM identity, articulation, routing,
phoneme, dynamics, generated-performance, audio-track, and style metadata is
reported as loss.

## Lifecycle boundary

`seam::authoring::InterchangeService` reads a bounded file, records its SHA-256,
and returns an unsaved draft. The current document is not changed until an
explicit conversion-review callback accepts it. Exports use create-new durable
publication and reject collisions without changing the project or destination
bytes. `.mid`/`.midi` use the same service and the bounded SMF codec.

This v1 is not a claim of complete OpenUtau interoperability: wave parts,
custom phonemizers/renderers, all expression curves, plugins, and every future
USTX field remain explicit losses. Installed-host and native-panel evidence is a
separate Beta-GO requirement.
