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
| Held/validated input handles | normalizedPath rejects empty paths, uninspectable paths and symlinks; readFileBytesLimited re-checks symlink_status and regular-file type at read time; the draft records sourcePath + sourceHash (SHA-256 of the bytes actually read). |
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
   (Conflict), "draft identity tracks the bytes actually read"
   (changed bytes -> different sourceHash), "parent replacement cannot
   silently redirect an import" (directory swap -> different
   sourceHash). Identity binds content, not path strings.
3. Interrupted output or destination conflict preserves the original
   file and current document: "exports create-new and preserves the
   project on collision" (conflict case), "export failure leaves no
   destination and preserves the project" (pre-write failure case),
   "rejects symlinked export destinations" (link target untouched).
   Interruption semantics rest on writeImpl's temporary-file publish
   with cleanup, which core atomic-write fault-injection coverage
   exercises in the autosave/manifest suites; the service adds no
   separate partial-write path.

## Verification run (this audit)

Release (build-u4-macos) and Debug (build/debug):
seam_interchange_service_tests 9/9 pass in both (3 pre-existing + 6
new scenario cases), seam_ustx_interchange_tests 31/31 in both,
seam_smf_interchange_tests 5/5 in both.

## Explicit non-claims

- USTX field coverage breadth (U30), real-DAW exchange (U31
  verification), native conversion-review panel (U32), and any
  host/platform qualification remain open.
- The boundary rejects symlinked paths; it does not claim a full
  hostile-filesystem threat model (TOCTOU between stat and open is
  bounded by the double symlink check but not formally eliminated).
