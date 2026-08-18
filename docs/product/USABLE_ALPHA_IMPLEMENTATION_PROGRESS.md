# Usable Alpha Implementation Progress

This document records implementation progress against
`docs/superpowers/plans/2026-08-18-usable-standalone-product.md`.
It is not a Usable Alpha completion claim.

## Completed implementation batch

### U0 — Baseline and product contract

- The canonical Usable Alpha acceptance contract exists in English and Korean.
- Stable requirement IDs `UA-001` through `UA-020` are machine-verifiable.
- PASS rows without evidence path and SHA-256 are rejected.
- Product documentation now states that the standalone executable is still a
  demo shell until the production authoring path replaces the hard-coded
  project and sine-wave playback source.
- Characterization tests preserve current CLAP project, voicebank, render,
  state, stale-publication, and Character/PCM-separation behavior.

Commits:

```text
1af7090 docs: define the usable standalone alpha gate
9e7a9a0 test: characterize authoring runtime behavior
```

### U1.1 — Shared `ProjectDocument`

Implemented a shared document owner around `EditorSession` and
`ProjectFactory` with:

- path-independent canonical project state;
- dirty-state tracking by revision;
- save and recovery identity;
- command execution, Undo, and Redo;
- project replacement with ID synchronization;
- failed-command transaction preservation.

Commit:

```text
4af04e6 refactor: add shared project document state
```

### U1.2 — Shared `VoicebankSession`

Implemented and integrated a shared voicebank session with:

- canonicalized, idempotent search roots;
- catalog refresh and candidate ownership;
- exact ID/version/content-hash resolution;
- explicit missing, version-mismatch, content-mismatch, content-hash-missing,
  untrusted, and invalid-reference results;
- ordered per-track resolution;
- undoable `SetTrackVoicebankCommand` binding;
- no silent candidate replacement;
- CLAP `EditorRuntime` delegation for scan, relink, binding, and resolution.

Commit:

```text
9faed3a refactor: extract shared voicebank session
```

## Fresh verification for this batch

The implementation batch must not be interpreted as complete until the
packaged repository independently passes the following checks:

```text
Usable Alpha contract tests
Usable Alpha contract verifier
Warnings-as-errors build
Named C++ tests
Phase 11/12A/12B regression CTest
Master-only branch policy
Dependency/license audit
Git diff check
Git object integrity
Clean extraction rebuild and retest
```

The package-verification report distributed with the ZIP is the evidence for
those checks.

## Next implementation task

The next plan task is **U1.3 — `AuthoringRenderCoordinator`**. It will extract
production project rendering, cancellation, stale-result rejection,
content-addressed PCM caching, progress, diagnostics, and realtime-safe
publication from the CLAP runtime into the shared authoring layer.

The remaining plan items stay unchecked. In particular, the current package
is not yet a usable standalone application and still does not satisfy the
Apple Silicon Usable Alpha acceptance contract.
