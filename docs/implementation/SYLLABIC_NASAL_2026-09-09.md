# Standalone Japanese syllabic nasal

A note containing only Japanese `ん` (`N`) can now render through its explicit
nasal pose without a fabricated vowel nucleus. The articulation plan uses that
note's fallback time span, or its authored start/end. The stream and snapshot
preflight accept this voiced-only nasal case. Other nucleus-free consonants and
multiple `N` tokens within one note remain unsupported by this allocation rule.

Candidate v3 accepts all-`N` output with one gesture per note. It still rejects
nasal kind relabeling, schema downgrade, absent/invalid models and duplicate-note
syllabic gestures. The producer test uses the existing `special:N` assignment,
imports as MarkerReview and recovers the nasal marker without review approval.

Tests extend actual Japanese phonemization, full rendering, Final baking, exact
candidate PCM comparison, loading and repository recovery. A direct stream test
requires nonzero output for a standalone note and rejection of ambiguous `んん`
allocation. No perceptual pronunciation judgment follows from these checks.

Articulation-plan revision 3 and stream revision 4 enter existing cache/render
identity. Recipe and candidate structural formats remain unchanged. This is a
specific syllabic-nasal extension, not complete Japanese singing or Beta GO.

The full Release build passed, followed by **5/5 focused suites (28.55 seconds)**:
voice design, performance snapshots, export, core and CLI production workflow.
This was not a new full 120-entry run; prior source-closure limitations remain.
Verification results accompany this report
under `evidence/syllabic-nasal-2026-09-09/`. No staging, commits, pushes or source/
musical approvals occurred. No captured file was missing relative to recovery
snapshot `session-preservation-W0sRkn`; existing dirty work was preserved.
