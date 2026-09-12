# Designer to existing producer workspace

Added an in-app opening path from source-free Designer to an existing producer
workspace. The entry screen has a third visible workspace button; the Designer
back action opens a workspace when none is loaded, and returns to the loaded
producer view otherwise. The voice draft is retained rather than replaced.

The macOS flow selects a directory, then explicitly requests the expected
inventory SHA-256 and existing operator ID. The controller verifies the digest
and, for this interactive route, requires a registered PRODUCER before adopting
the recovered state. Unknown and reviewer-only identities reject. Cancel and
failed validation leave the existing context intact. Captured Designer revision
and producer epoch are checked again after the dialogs.

The existing lower-level opening API retains its legacy default behavior for
existing callers; the new interactive path explicitly enables producer-role
checking. Registration is not authentication, and opening grants no new role,
source permission, review approval or publication. Mutation commands retain
their separate existing role/context checks.

Opening does not initialize microphone input. If the user subsequently starts
recording in the loaded producer workspace, input initialization occurs then.
No microphone action was exercised in this QA run.

## Verification

Full Release build and four focused suites passed in 48.24 seconds: Designer,
export, aggregate core and Studio manifest draft. The new test checks unknown
and reviewer-only operator rejection, mismatched inventory rejection, unchanged
session/project on failure, and successful adoption with the registered producer.

Live macOS check:

1. Created a draft and changed aspiration to 0.12 without saving.
2. Opened the folder and binding dialogs from Designer and supplied the known
   engineering workspace's inventory hash and producer ID.
3. Producer view showed PRODUCTION RECOVERED and the existing cv:ka assignment,
   still in MarkerReview with no approval and microphone off.
4. Returned to Designer: the same epoch/revision and unsaved 0.12 setting remained.
   Prepare generation job became enabled for the loaded assignment.

The immediate return shortcut did not act after the modal; a canvas click was
needed before Command-D worked. This focus-continuity defect remains open.
Subsequent repair and focused live verification are recorded in
`STUDIO_NAVIGATION_FOCUS_2026-09-09.md`; that follow-up resolves this specific defect.
No generation job, recording or publication was triggered. The temporary
in-memory QA draft was not saved and the test process was stopped.

## Remaining scope

This opens existing workspaces, not new-workspace creation or replacement of an
already loaded workspace. The later `ASYNC_WORKSPACE_RECOVERY_2026-09-09.md`
increment moves initial recovery off the UI thread; large-workspace responsiveness
is still not qualified. The initial picker was macOS
only; other platforms return an explicit unavailable result until implemented.
Inventory selection, operator registration, complete source-to-bank onboarding,
focus repair and full Beta GO acceptance remain unfinished.

Evidence logs are in `evidence/designer-workspace-2026-09-09/`. No files were
missing relative to `session-preservation-jZ9Vs2`; no staging, commit or push
occurred. No whole roadmap unit is accepted by this increment.
