# Native USTX interchange subset v1

Project SEAM now has a native, bounded USTX 0.6–0.9 import boundary and a
deterministic USTX 0.9 export boundary. This is an actual
codec and conversion path, not the earlier creator-study Python bridge.

## Accepted input

- UTF-8 USTX 0.6, 0.7, 0.8 or 0.9 YAML with one mapping document. The source
  version remains on the decoded document; re-export writes 0.9. OpenUtau's
  pinned `Ustx.Load` migration changes expression selectors when loading 0.6,
  which are outside this musical subset. Versions before 0.6 use a different
  timing-map form and are refused; future versions are refused until reviewed.
- Block mappings/sequences and flow mappings/sequences used by OpenUtau's
  ordinary `tracks`, `voice_parts`, `notes`, `tempos`, and
  `time_signatures` records.
- Tempo positions and note/part positions use USTX's 480 PPQ grid.
- Note pitch points use OpenUtau's millisecond `x` offset and tenth-semitone
  `y` offset. `l`, `sp`, and the common smooth shapes are mapped to SEAM
  linear, step, and smooth interpolation.
- Note vibrato is mapped from USTX percentage/frequency fields into the
  persisted SEAM vibrato contract with bounded clamping diagnostics.
- OpenUtau's simple terminal Japanese lyric hint (`あ[k a]`) maps to the
  visible lyric `あ` plus a typed SEAM phone hint only when SEAM's Japanese
  phonemizer accepts its space-separated phones. Both quoted and unquoted
  YAML plain scalars are accepted. Unsupported phones or languages produce a
  loss and remove the bracket suffix from the visible lyric; complex bracket
  syntax remains raw lyric with an explicit loss. No arbitrary phonemizer or
  singer is loaded to interpret it.
- A standard `dyn` part curve maps integer tenths of a decibel at part-relative
  480-PPQ ticks to SEAM linear-gain automation. OpenUtau's `-240` sentinel
  means silence; other values use `10^(y/200)`. The importer samples its
  five-tick render grid, retaining the default unity gain outside the curve.
  Custom `dyn` descriptors, duplicate/unsupported curves and curves outside
  the part are omitted with explicit losses.

The reader rejects aliases/anchors/tags, multiple documents, duplicate keys,
tabs in indentation, malformed flow syntax, non-finite numbers, invalid UTF-8,
and any input or collection that exceeds `UstxLimits`. Limits are checked while
parsing, before typed arrays are grown from declared values.

## Conversion semantics

Import creates a new unsaved `domain::Project` draft. It scales 480 PPQ to the
canonical 960 PPQ model, retains tempo/meter, track mix fields, notes, lyrics,
pitch automation, vibrato, representable Japanese phone hints and supported
`dyn` automation, and carries an
explicit bounded loss/warning list. Singer references, voice colors,
renderer/phoneme details, other expression curves, SEAM ownership/units, and
other unsupported controls never become executable state by accident. A `dyn`
span requiring more than SEAM's 16,384 automation points is omitted with an
explicit loss while the rest of the score can still import.

Export is deterministic and emits a USTX 0.9 subset. Project ticks are rounded
to 480 PPQ with warnings; millisecond pitch positions are derived from the
authoritative tempo map. Unsupported SEAM identity, articulation, routing,
phoneme overrides, generated-performance, audio-track, and style metadata is
reported as loss. Valid Japanese phone hints are written in OpenUtau's
terminal bracket syntax; other SEAM hints are reported as losses. SEAM dynamics
are written as OpenUtau `dyn`; 0.1 dB value quantization,
480-PPQ tick collisions and interpolation differences between sparse points
are explicitly reported. A nonzero gain closer to silence than to OpenUtau's
minimum nonzero level is exported as its `-240` silence sentinel with a loss.

## Lifecycle boundary

`seam::authoring::InterchangeService` reads a bounded file, records its SHA-256,
and returns an unsaved draft. The current document is not changed until an
explicit conversion-review callback accepts it. Exports use create-new durable
publication and reject collisions without changing the project or destination
bytes. `.mid`/`.midi` use the same service and the bounded SMF codec.

This v1 is not a claim of complete OpenUtau interoperability: wave parts,
custom phonemizers/renderers, expression curves other than standard `dyn`, plugins, and every future
USTX field remain explicit losses. Installed-host and native-panel evidence is a
separate Beta-GO requirement.

The compatibility regression includes files emitted by the actual OpenUtau
0.6, 0.7, 0.8 and 0.9 serializers (see `tests/fixtures/ustx/README.md`). These
carry their UTF-8 BOM and indentless block sequences; the bounded parser accepts
both. The earlier version-adjusted fixture remains a smaller control, not the
source of historical evidence. Nonzero OpenUtau note `tuning` is incorporated
into SEAM pitch automation, preserving the tested musical contour, but its
separate edit control is not retained and is explicitly reported as loss.
`tuning` must be integral to match OpenUtau's `UNote.tuning` type. The
curve-bearing historical-serializer fixture has a 64-point `dyn` curve; SEAM
now imports and re-exports it as dynamics. Large expression curves can still
exhaust the bounded line/collection budgets before note import. A sixth fixture exercises the
historical serializer's folded `>-` multiline comment, which now imports.
The reader supports literal (`|`) and folded (`>`) block scalars with strip,
clip or keep chomping and single-digit explicit indentation, subject to the
same byte, physical-line/node and UTF-8 limits. This is a bounded subset of
YAML, not an arbitrary-YAML promise. Nonempty project and part comments are
reported as losses because SEAM does not retain them. SEAM's own writer emits
an empty comment, avoiding a fabricated loss when its output is reimported.
Serializer-generated files do not substitute for broad real-world documents or
actual desktop GUI open/save verification; both remain part of U30 acceptance.
