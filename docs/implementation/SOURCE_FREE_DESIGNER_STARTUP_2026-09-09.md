# Source-free Voice Designer startup

Launching Studio without arguments now opens the Designer entry screen instead
of printing usage and exiting. It provides visible New voice draft and Open
saved recipe buttons with shared painting, pointer and accessibility bounds.
No installed bank, recording, producer ID or synthetic placeholder assignment is
created for this entry route.

`--designer` selects the same view explicitly and can be combined with normal
manifest/producer arguments. Existing producer identity/digest validation remains
mandatory. Source-free `--record-ms` is rejected. A source-free Designer launch
returns before input-device initialization; output audition is still available.
Ordinary manifest/producer workflows retain their input behavior.

## Live verification

- The no-argument executable opened directly in Designer with New/Open actions.
- An initial close exited normally and reported `input_backend=unopened`,
  `input_physical=false`, zero input callbacks, zero input frames and no recording.
- After the visible entry buttons were added, the final app screenshot showed
  both controls without overlap at the minimum window size.
- A coordinate click on New voice draft created editable recipe state.
- Render current audition reached Vowel ready without a supplied voicebank.
- Generation-job preparation remained disabled without a producer workspace
  and selected assignment. No source approval or producer identity was invented.

The second, test-only in-memory draft was not saved and its process was stopped.
No user recipe was changed. Opening a saved recipe uses the existing dialog and
session path; this turn did not requalify every platform's file dialog.

Option tests cover no-argument and explicit entry, bank-backed Designer entry,
unknown arguments, forbidden source-free recording, and incomplete producer
arguments. Full Release build passed; aggregate-core tests passed in 20.72 seconds.
Logs are retained under `evidence/designer-startup-2026-09-09/`. This was not a
new full-suite run. Full producer-workspace onboarding, file associations, signed app
distribution, native visual polish and singer quality remain separate unfinished
requirements of the original Full-Scope Beta GO plan.

No files were missing relative to `session-preservation-alfiVF`; existing dirty
work remains preserved without staging, commit or push. The prior full-regression
source-closure failure remains open, not bypassed by this launch path.
