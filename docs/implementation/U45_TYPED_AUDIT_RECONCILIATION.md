# U45 typed audit reconciliation

2026-10-09. Adapted from archived `2a3d23e0` onto `ed5f9a5d`.

Status: **engineering prerequisite; U45 and full Beta acceptance remain incomplete**.
The canonical full-product contract still declares its semantic validator
`UNAVAILABLE`. No qualification profile, released resource, human decision or
release state is changed by this increment.

## Executable boundary

`tools/external_beta/full_product_gate.py` checks resource declarations, case-axis
coverage, retained review/measurement record consistency, selected artifact
syntax, and candidate/workload/content digests. It runs the existing full reader
first with the explicit archive root and the same `SoakReplayContext`, including
its guarded snapshots, aliases and terminal read denials. The typed evidence
cache consults that context before its own cache and has a 256 MiB aggregate
budget for ordinary retained files. Both successful and failed ordinary reads
are memoized for the audit.

The report schema now requires `evidenceClass` and `reviewerRegistry`. The
reserved synthetic contract cannot authorize READY or CLOSED. The existing
reader fixture adds this metadata and the case-specific criterion measurements;
its legacy soak refusal remains unchanged. Public replay continues to run the
real validators with its original four exact soak refusals, without an audit
mock or fixture-admission bypass.

`verify_full_product_report.py` runs the typed adapter. It uses a bounded,
no-follow top-level report snapshot and accepts `--evidence-root` (default:
report directory). BLOCKED exits 3. A hypothetical complete synthetic report
would exit 5 with `authorizesRelease: false`; the current archived fixture is
BLOCKED because its soak blobs are not supervised-session records. Exit 0 is
reserved for canonical authorization, which the explicit
`U45_RECONCILIATION_HOLD` currently prevents.

The release gate still calls the existing report reader. **Do not replace that
call or remove the reconciliation hold as part of an incidental cleanup.**
A later reviewed integration must preserve EB-005/R17 shared context and all
existing soak refusals, while closing the semantic gaps below.

## What the checks prove

- All 83 archived component case rows can satisfy their declaration checks in a
  temporary synthetic fixture. This is separate from full-report acceptance.
- WAV parsing checks exact RIFF/chunk bounds, complete integer/float frames,
  finite samples and nonzero signal. Float negative zero is silence; extensible
  subtype GUID and valid-bit width are checked. This is not singing, language,
  timbre or audible-quality validation.
- PNG parsing supports noninterlaced 8-bit RGB/RGBA screenshots with checked
  chunk CRCs, contiguous compressed image data, complete bounded scanlines and
  legal filter bytes. Decoded data is capped at 64 MiB. Other PNG encodings are
  refused explicitly. It does not prove what the depicted controls did.
- Project parsing checks the SEAM document envelope only. It is not a native
  codec load, recovery or editor replay.
- Reviewer registry and measurement constraints are retained declarations. The
  adapter checks consistency and self-approval constraints; it does not prove
  that a declared person exists, is independent, or listened to the material.

## Remaining integration work

1. Add a separate closed U14 engineering-execution record and validator using
   actual schema-3 candidate, signed package verification, native install receipt
   and installed candidate bytes. It must return engineering success without
   release authorization, reject human/qualification fields, and stay distinct
   from the 83-case release report. Test positive sample/recipe execution and
   opaque model package verification with intentional install refusal.
2. Bind every required installed resource to a retained real receipt. The
   archived `installReceiptSha256` field alone is only a declaration, and its
   conditional missing-receipt check is insufficient. Require a release
   candidate for release-installed identity checks.
3. Preserve U14 family-specific identities: sample candidate `contentSha256`
   matches receipt `contentHash`; recipe content SHA identifies the recipe,
   while the installed singer hash covers manifest plus recipe. Keep resource
   candidate SHA distinct from release candidate root identity. Runtime
   dependencies carry kind/id/revision; do not invent package digests for them.
4. Replay native project, operation, continuity and measurement semantics.
   Distinct file hashes and `MET` claims do not establish those behaviors.
   Human constraints need real retained reviews and external authority binding;
   an arbitrary retained file cannot stand in for a human protocol judgment.
   Each observation also needs a native provenance record binding platform,
   host, build, installed tree, resource, language and artifact digests, plus
   native project loading that confirms the language/resource bindings. A
   duplicate-digest ban is not a substitute: deterministic output may legitimately
   be identical across platforms. These are release-blocking P1/P2 conditions,
   together with actual receipt verification, before lifting the hold.
5. Review coverage obligations beyond the archived independent-axis coverage:
   per-resource/per-case combinations, dependency resolution and approved
   incompatibilities must match the frozen contract. No platform/host waiver.
6. Integrate the completed executor into READY/CLOSED with the existing shared
   soak context. Retain engineering-only and synthetic-contract refusals.
   Freeze the canonical profile only with the designated independent owners;
   validate real installed resources and externally anchored human evidence.

Full acceptance still requires the unaided 20-row production-bank UA journey,
learned singing and language/creator evidence, Windows and nine host tuples,
signed installed workloads and authorized immutable-archive release decisions.
None is inferred from the synthetic checks. `.github` and Windows TODO remain
unchanged.
