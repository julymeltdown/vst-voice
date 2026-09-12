# Return to Designer after unsuccessful workspace entry

Studio now detects when an initial workspace-opening worker finishes without
adopting a project. Failure or cancellation returns to the retained Designer
session rather than leaving an empty producer view. Failure retains the actual
diagnostic; cancellation explicitly distinguishes a retained draft from an empty
entry screen. Successful workspace opening still reaches the producer view.

The controller exposes its specific opening state separately from aggregate
producer busy state, so an unrelated import/review completion cannot trigger this
navigation. Tests assert that cancellation and invalid-operator completion clear
that state without adopting a project.

Live macOS check created an unsaved draft with aspiration 0.17, then attempted
the existing engineering workspace using a deliberately mismatched inventory
digest. The app returned automatically to Designer with the same epoch/revision,
0.17 value and unsaved state, plus the digest-mismatch diagnostic. Pressing Right
immediately changed the selected value to 0.18 without a canvas click. No recipe
or producer file was saved; the temporary in-memory test session was stopped.

Full Release build and three focused suites passed: Designer, aggregate core
and Studio manifest draft (22.90 seconds). Logs are retained under
`evidence/workspace-entry-recovery-2026-09-09/`. This is not a fresh full-suite run.
The live check qualifies failure recovery, not cancellation latency during slow
filesystem I/O or every native loading interaction. Full Beta GO remains open.

No files were missing relative to `session-preservation-36TzCf`. Existing dirty
work remains preserved; no staging, commit or push occurred.
