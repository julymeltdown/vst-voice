# Installed-song review and later execution supplement

Date: 2026-10-15, Asia/Seoul. Integrating base: `770dd92319497422a430922cb59a99d4814dd110`.
**Engineering evidence only; Beta remains BLOCKED.**

The independent **Review in English** verdict approves exact
`fe340737c0709a45f1d48cdfec8ec0089ea814ce`, with no required code change.
It confirms the installed 48-note song lane, provenance-free Final identity
check, limits and overflow guard. The complete 17,208-byte verdict is retained,
SHA-256 `d59b8c5051028dba4d347e8e0f349b962ec6ee9577f5c5f5d00c885dfe521cd2`.
This is not an approval of subsequent changes or human/product acceptance.

## Review findings and scope

The P2 finding is an evidence limitation: the committed runtime source was
written after the only original build that compiled it and after the original
passing CTest. Those logs show a passing plugin, but do not establish that the
complete committed bytes were its compiled source. The frozen historical root
remains unchanged; the earlier dossier now states this limitation explicitly.
No claim that the intervening write was content-neutral can be substantiated here.

The P3 asks for a small native provenance regression instead of relying solely
on the Python/plugin/full-song lane. It is added to
[native Final completeness tests](../../tests/test_final_render_completeness.cpp).
A generated two-phrase sample-bank fixture prepares Final with absent provenance,
then with saved-export ABI/compiler fields. Each uses a new runtime and requires
successful preparation, Ready audio and nonzero samples. There is no Preview
polling or manual warm-up. This is one native case with two control preparations,
not two independently counted cases or a qualified recording.

## Controlled failure and actual repaired execution

A controlled mutation restores the former provenance-including source encoding
in the current runtime. It changes only that expression; it is not execution of
the full historical parent build. The new native case fails after its unstamped
control succeeds: the stamped project reports that the published source differs
from the prepared timing identity. The exact mutated and fixed sources are retained.

The initial wrapper correctly received the native failure but then searched stdout
for the message that the framework wrote to stderr. Its `finally` block already
restored fixed source byte-for-byte. The wrapper, raw native streams and receipts
are retained. A corrected read-only validator confirms the intended failure and
exact restored Git bytes; no successful mutation run is invented.

After restoration, the full untargeted Release build passes in **22.636s** and
explicitly recompiles `editor_runtime_project.cpp`. Its retained fixed bytes match
both `fe340737` and the integrating base, SHA-256
`f33adc8418048c7d992b655f1901508cf130f2699d81c591a0d64e585d599a99`.
Two CTest entries pass: **13 native completeness cases and eight Python host methods**.
The initial run takes 11.975s; the final repeat takes **10.768s** with explicit
before/after source, host, plugin, native test and child fixture binary hashes.
Earlier receipts have narrower bindings; their original helper is retained.
These repeated runs are not added together. One native case is new.

A separate freshly executed direct host run takes **0.567s**, loading the retained
original caller-pinned installed song through the real plugin. Its receipt binds
host, plugin, project and source before and after execution. Public state round-trip,
Final/rate-change/negative/transport/overflow controls pass, with **96 note windows**
and **3,845,175 frames summed across two rates**. The retained 48 kHz float32 stereo
WAV has **2,004,000 frames**, 16,032,044 bytes, finite samples and peak 0.1054771468.
It is byte-identical to the original frozen song WAV, SHA-256
`581d9404098f8965c9d961653f28051adb50d0650b76786c6401a5b2445ae9a9`.
Only the 48 kHz PCM is retained; the 44.1 kHz result rests on host assertions.

This supplies later execution evidence for the committed repair. It does not
authenticate historical execution, provide hermetic build lineage, or perform
fresh package-signature verification. The song still uses a legacy procedural
test resource and historical absolute installation dependencies. Replay is not
self-contained; the negative Final controls still reuse a warmed score instance.

## Retention and next work

The [evidence index](INSTALLED_CLAP_SONG_REVIEW_2026-10-15.evidence.json) binds
**39 artifacts and eight preserved inputs** under
`out/evidence/installed-clap-song-review-2026-10-15`. Its 7,949-byte manifest has
SHA-256 `fbaf03c81b849bee46fbeebd0b4d86132eafebf65f67786fcd641eab0b628092`.
The verifier reads hashes, receipts, Git and PCM; it executes no captured runtime
code or plugin. `--source-commit` binds this new native test to its immutable commit.
Canonical gates and earlier frozen roots remain unchanged.

The new test and supplement await their own independent review. The peer has
started exact `c27d1d6f` publication/revocation review. Later pitch and host admission
units remain unreviewed. Safe fixture cleanup, durable historical audit from retained
copies, exclusive output publication and semantic request/timeline integration remain.
No full-suite, GUI, physical-device, external-DAW, Windows, human or listening
acceptance is claimed by these focused checks.

The 297 internet originals remain downloaded, unchanged and unqualified. Source/
performer/identity/public-license disposition, genuine producer and distinct reviewer,
phonetic coverage, failed-QC ownership and listening still block a released bank.
Full classical/neural/R9 resources, acoustic tolerance ownership and user/DAW/platform/
release acceptance remain required. Alpha stays at **0/20 recorded passes**, U45 is
held, Beta is **BLOCKED**, and the full goal remains ACTIVE. `.github`, README Windows
TODO, original recordings, prior frozen roots and the existing stash are preserved.
