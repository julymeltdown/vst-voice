# Direct Technical-lane Editing

Phase 12B converts the embedded technical lanes from read-only inspection into
project-backed edits.

## Phoneme boundary

A selected `PhonemeKey` can update its start or end offset. The operation writes
a `PhonemeOverride`, validates the region, commits through `EditorSession`,
invalidates the affected render revision and remains undoable.

## Unit selection and renderer

A unit boundary can:

- select an exact candidate unit ID;
- cycle deterministic alternatives;
- select or cycle Raw, Classic PSOLA, SpectralClassic and Stretch;
- retain a lock so replanning does not silently replace the user's choice.

The selected unit and requested renderer are stored in canonical project state.
The actual renderer and any explicit fallback remain visible in the Unit Lane.

## Pitch automation

The active region supports:

- insert/update at a musical tick;
- move to a new tick and value;
- remove;
- cycle Step, Linear and Smooth interpolation.

Pitch is stored in cents relative to the note pitch. The render snapshot includes
neighbour anchors so phrase boundaries do not truncate interpolation context.

## Sample Microscope

The selected unit opens the existing Sample Microscope model with waveform,
spectrogram, acoustic markers and pitch marks. Project-side selection is separate
from Voicebank-authoring marker edits: the plug-in may inspect the installed bank
but does not mutate signed installed Voicebank content.

## Interaction contract

Mouse hit-testing is partitioned by lane so note drags cannot begin over the
Phoneme, Unit, Pitch or Microscope panels. A drag previews its new value and
commits one command on pointer release. Keyboard Undo/Redo uses the same command
history as the standalone editor.
