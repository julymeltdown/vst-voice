# U29 acceptance audit: bounded interchange I/O and report boundary

Date: 2026-09-22. Plan criterion source:
docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md (U29).
Verdict: LOCAL IMPLEMENTATION ACCEPTANCE - every explicit U29 criterion
and test scenario maps to named code and passing test evidence. This is
not Beta GO, not a claim about U30/U31 codec coverage, and not a
native-panel or cross-platform claim.

## Criteria to evidence

| Plan requirement | Evidence |
|---|---|
| Parse external scores without mutating the current document | InterchangeService::importFile returns InterchangeImportDraft (an unsaved domain::Project plus issues); no reference to the active document exists in the service API. Test "imports an unsaved USTX draft with source identity". |
| Do not trust file-declared sizes | core::readFileBytesLimited stats the real file size and rejects size > maximumBytes before allocating the buffer; UstxLimits/SmfLimits bound depth, nodes, collections, scalars, tracks, parts, notes, tempo/meter events, curve points and ticks inside the codecs. Test "rejects oversized input before codec work" (16-byte cap rejects a valid fixture with ErrorCode::Unsupported). |
| Held/validated input handles | normalizedPath rejects empty paths, uninspectable paths and leaf symlinks, and canonicalizes the parent chain so intermediate symlinks resolve deterministically; on POSIX readFileBytesLimited opens once with O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK (a FIFO cannot block admission before the S_ISREG rejection), fstat-validates the descriptor, reads every byte from that SAME descriptor via pread, then a post-read fstat compares dev/ino/size/mtime/ctime - ctime is kernel-maintained and cannot be restored by user-space timestamp calls, so a same-size rewrite with a futimens-restored mtime still rejects (ErrorCode::Conflict). The descriptor is scoped-owned so an injector throw or allocation failure cannot leak it. The metadata comparison is a best-effort snapshot check, not a proof of immutable content. The draft records sourcePath + sourceHash (SHA-256 of the bytes actually read). Windows retains the prior stat-then-read sequence pending platform evidence (see non-claims). |
| Byte/event/object budgets | UstxLimits.maximumInputBytes (4 MiB) and SmfLimits.maximumBytes (64 MiB) gate the read; per-structure codec limits gate amplification. |
| Bounded diagnostic amplification | InterchangeService::append caps issue records at 4096 per import/export. |
| Draft conversion result | Import produces an inert draft + bounded InterchangeIssue list (warning/loss severities preserved from codec issues). |
| Export stages a complete file before publication | core::durableAtomicWriteNew writes/syncs a temporary then publishes create-new; a conflicting destination fails with ErrorCode::Conflict and the pre-existing file is byte-identical afterward (test "exports create-new and preserves the project on collision"). |
| Shared boundary between USTX and SMF | One service entry point selects format by extension (.ustx/.mid/.midi), applies the matching limits, and returns the same draft/receipt shapes for both codecs. |

## Plan test scenarios to tests

1. Oversized input and declared allocations exceeding budgets fail
   before allocation: "rejects oversized input before codec work"
   (service-level byte cap, ErrorCode::Unsupported) plus codec-limit
   rejections inside seam_ustx_interchange_tests (31 cases) and
   seam_smf_interchange_tests (5 cases).
2. Symlink/reparse/parent replacement and changed input bytes cannot
   redirect a held import silently: "rejects symlinked import paths"
   (Conflict), "canonicalizes an intermediate symlink component"
   (admitted path resolves to the real directory; sourceHash and
   sourcePath identical to the direct import), "a held import reads
   the originally opened inode across parent replacement" (fault
   injector swaps the parent directory between open and read; the
   descriptor pins the original inode so the sourceHash matches the
   baseline), "a held import rejects in-place mutation during the
   read" (injector rewrites the same inode post-read; post-read fstat
   rejects with Conflict), "a held import rejects a same-size mutation
   with a restored mtime" (injector rewrites same-length content and
   restores mtime via utimensat; the ctime comparison still rejects),
   "a held import rejects a FIFO without blocking the admission"
   (detached worker bounded by a 5-second deadline; O_NONBLOCK makes
   open return so S_ISREG can reject), "a held import closes its
   descriptor when the admission throws" (injector throws; /dev/fd
   count is unchanged), "draft identity tracks the bytes actually
   read" (changed bytes -> different sourceHash). Identity binds
   content, not path strings.
3. Interrupted output or destination conflict preserves the original
   file and current document: "exports create-new and preserves the
   project on collision" (conflict case), "export failure leaves no
   destination and preserves the project" (pre-write failure case),
   "rejects symlinked export destinations" (link target untouched).
   Interruption semantics rest on writeImpl's temporary-file publish
   with cleanup. Shared-path fault-injection coverage exists at
   tests/test_stabilization.cpp:235 ("durable atomic write preserves
   old data across injected faults") for the Replace publish mode;
   the export path uses the CreateNew mode, which shares
   writeAndSyncTemporary + cleanup but publishes via link(2) instead
   of rename(2). Distinguish pre-publication failures (temporary
   cleaned up, no destination; tested) from post-publication errors
   (unlink of the temporary or parent-directory fsync fails after a
   successful link: the destination is already complete; the error is
   reported but cannot unpublish). CreateNew-mode interruption
   between link and unlink is NOT directly fault-injection tested;
   the worst case is an orphaned temporary file beside a complete
   destination, not a torn destination.

## Verification run (this audit)

Release (build-u4-macos) and Debug (build/debug):
seam_interchange_service_tests 15/15 pass in both (3 pre-existing +
12 scenario/boundary cases), seam_ustx_interchange_tests 31/31 in
both, seam_smf_interchange_tests 5/5 in both. The POSIX-only cases
(leaf/intermediate symlink, injector-dependent held-admission, FIFO,
descriptor-leak) are registered only on POSIX; they are not counted
as trivial passes on Windows, matching the pending-Windows non-claim.

## Explicit non-claims

- USTX field coverage breadth (U30), real-DAW exchange (U31
  verification), native conversion-review panel (U32), and any
  host/platform qualification remain open.
- On POSIX the read is descriptor-held: leaf-symlink and parent
  replacement cannot redirect it, and in-place mutation during the
  read is rejected. The post-read metadata check is best-effort:
  metadata equality alone is not a proof of immutable content, and an
  attacker who can mutate the file between the post-read fstat and the
  caller's use of the bytes, or control mount options, is out of
  scope.
- The Windows branch of readFileBytesLimited retains the prior
  stat-then-read sequence (symlink_status + size + ifstream); the
  held-input guarantee is POSIX-only until Windows reparse/descriptor
  semantics are verified on platform hardware. Windows support is a
  documented TODO.
- The fault injector is a test seam; production callers pass none.
