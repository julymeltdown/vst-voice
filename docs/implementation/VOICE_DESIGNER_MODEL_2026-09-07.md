# Voice Designer editable draft model

`VoiceDesignerModel` implements the owner-thread editable recipe state required by U22. This is a model component, not completed Designer UI or acoustic acceptance.

## Implemented behavior

- Creation validates/freezes an existing `VoiceRecipe`. New models are dirty until an exact persisted snapshot is acknowledged.
- An edit supplies an expected model revision and the desired complete recipe. This lets one slider gesture or grouped control change become one undo item. All existing recipe/source/resonance/frication validation applies before mutation.
- Recipe ID and engine ID are fixed within a model; a distinct voice requires a separate model. Parameter changes still alter the canonical resource hash and can change recipe schema version when articulation is added or removed.
- Accepted changes publish a new immutable procedural resource. Previously copied resources remain unchanged and can safely identify earlier previews or jobs.
- No-op edits preserve revision and redo history. Failed validation and stale revision checks preserve recipe, resource and history.
- Undo/redo restores recipe/resource values while incrementing a monotonic revision, so undo cannot revive an old UI action. Each history direction is bounded to 128 snapshots; a branched edit clears redo.
- Dirty state compares the current canonical content hash with the acknowledged saved hash. Save acknowledgement requires the current revision and exact resource hash. It must be called only after successful external persistence; it is not itself file IO or proof of persistence.

The model owns no song, producer repository, installed bank or source approval. Valid draft values do not imply every backend can render every parameter, nor do they establish female-voice quality, intelligibility or commercial-source qualification.

## Verification and remaining work

### Continuous control gestures

`beginGesture`, `updateGesture`, `commitGesture` and `cancelGesture` now support continuous preview edits. Each accepted preview replaces the immutable resource and advances the model revision without growing undo history. Commit stores the starting snapshot once if final content differs; cancel restores it without changing undo/redo. A gesture that returns to its starting content also preserves redo. Beginning and closing a gesture advance revision even when content is unchanged, preventing an earlier preview/action from being mistaken for a new interaction. Preview updates reserve a revision for closing the gesture at counter exhaustion.

Nested gestures, ordinary edits, undo/redo and save acknowledgement are rejected while a gesture is active. Invalid preview parameters leave the last valid preview and gesture intact. Existing copied resources remain immutable after cancel or undo. Gesture preview is recipe state, not a claim that audio is currently playing; asynchronous audition and save initiation must respect these boundaries when connected.

Two additional tests exercise 100 preview updates collapsing to one undo, stale completion rejection, immutable preview snapshots, invalid preview recovery, cancelled/neutral gesture redo preservation and exact saved-identity restoration. All five Designer tests pass in strict Debug/Release (0.44/0.48 seconds); diff checks pass. Live slider/audition interaction remains unimplemented.

Three focused tests cover combined phonation/resonance edits, immutable old resources, dirty/save identity, stale-action rejection across undo, no-op redo preservation, invalid/non-finite/identity edits, branch behavior, v1/v2 articulation transitions and the 128-entry limit. Strict Debug/Release builds and focused suites pass (0.41/0.47 seconds), and `git diff --check` passes. The tests are also registered in the main native suite, which was not rerun for this checkpoint.

Still required: Studio ownership and Create/Open/Save actions, visible grouped controls and gesture transactions, asynchronous audition with stale-result guards, A/B/preset workflow, generation integration from the current draft, complete accessibility interaction and listening/production qualification. Full U22/Beta GO remains incomplete.
