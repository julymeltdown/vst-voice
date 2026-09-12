# Voice Designer file session

`VoiceDesignerSession` now owns an optional editable draft, its file path/saved identity, a document epoch and one asynchronous file worker. It is independent of the producer workspace and does not mutate songs or installed banks.

## Lifecycle

- Create validates a supplied recipe and opens it as an unsaved draft. Replacing a dirty draft requires explicit `discardUnsaved`; an active gesture cannot be replaced.
- Open loads/freezes a bounded recipe on the worker and adopts a clean model only after successful completion. Failed/cancelled loading preserves the current draft, path and saved identity.
- Save snapshots the recipe/revision/hash and prevents session edits while work is pending. Save As is new-file-only. Saving the current normalized path first reloads its canonical identity and rejects an external change, advising a new destination.
- Writers acquire a persistent `.designer.lock` sibling through the shared nonblocking OS lock. The lock is retained on disk and ownership is released on close/process exit. This coordinates sessions using the same normalized path, not arbitrary external editors or aliases with different lock paths. The read-check/atomic-replace sequence is not a filesystem compare-and-swap against noncooperating writers.
- A successful save acknowledges the exact current snapshot, retaining undo/redo. Failure preserves dirty state and history. Once atomic publication starts, a late cancellation does not hide success.
- Successful create/open advances the document epoch. Edits/undo/redo require both that epoch and the model revision, so old-document actions cannot target a newly opened voice whose revision happens to match.
- `finish` requests cancellation, joins and collects the actual result; destruction requests cancellation and joins to keep worker lifetimes safe. UI close logic still needs to decide whether to save/discard a dirty draft before destroying its session.

No UI action should interpret a recipe save as bank approval, a source-rights decision or automatic regeneration of old jobs. Saved song/job resource references keep their original identities.

## Evidence and remaining work

Two added tests cover create/save/edit/save/reopen, busy edits, dirty replacement rejection, old-document action rejection, failed-open preservation, external file changes, existing Save As protection, lock contention, successful Save As and shutdown cleanup. All seven focused Designer tests pass in strict Debug/Release (0.48/0.55 seconds), and `git diff --check` passes.

Studio hosting, visible Create/Open/Save/close prompts, gesture forwarding, parameter controls and asynchronous audible preview remain unfinished. This is the file-session component, not full U22 or Beta GO acceptance. No physical listening or arbitrary process-interruption qualification was performed here.
