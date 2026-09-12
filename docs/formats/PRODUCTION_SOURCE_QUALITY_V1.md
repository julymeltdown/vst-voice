# Production source quality assessments v1

## Scope

This records an explicit supplied coverage/listening decision for the current
source policy and active audio material. It does not grant source-use,
transformation, bank-redistribution or commercial-render rights; perform actual
acoustic measurements; authenticate a real-world reviewer; approve units; or
authorize release. Independence checks compare registered reviewer IDs against
the recorded importers and processing operators.

Initial source-aware Draft creation remains schema 2. The first recorded quality
assessment opts the current producer generation into schema 3. Historical schema
1/2 bytes are not rewritten; old readers reject schema 3 rather than silently
discard its evidence. Schema 3 requires `sourceQualityAssessments`; older schemas
reject that field. Existing schema-2 feasibility declarations retain their prior
admission behavior, not retrospective qualification by this new workflow.

## Assessment record

Each record has exactly these fields:

```json
{
  "id": "quality-review-001",
  "strategyId": "original-source",
  "policySha256": "<64 lowercase hexadecimal characters>",
  "materialSha256": "<64 lowercase hexadecimal characters>",
  "evidenceSha256": "<64 lowercase hexadecimal characters>",
  "reviewerId": "<registered independent REVIEWER>",
  "reviewedAtUtc": "<UTC timestamp>",
  "coverage": "PASS",
  "listening": "PASS"
}
```

Outcomes are `PASS`, `BLOCKED`, or `NOT_ASSESSED`. IDs are unique and nonempty,
at most 128 bytes. At most 1,024 assessments are decoded into typed history.
The list is append-only; the canonical save path permits exactly one addition
with a matching `source-quality-assessment` journal subject, reviewer and time.
Removing, rewriting or downgrading existing history is rejected.

## Identity and eligibility

The policy identity hashes canonical compact JSON tagged
`seam-source-quality-policy-v1`, containing strategy ID/kind, rights outcome,
license locator/hash, and all four explicit permission booleans. Coverage,
listening and the descriptive evidence-state label are not policy identity.

The material identity hashes canonical compact JSON tagged
`seam-source-quality-material-v1`. It includes project/inventory identity and
active source-owned takes sorted by take ID. Each row binds assignment
coverage/pitch/prompt, source-binding ID, raw and effective audio digests, current
processing parent, importer and processing operators. Shared WAV bytes do not
merge take ownership. Annotation review is separate. Future language/style
assignment migration must deliberately version/reconcile this identity.

C++ and Python implementations are checked against the actual CLI. The command
also requires the exact captured producer-project SHA-256. Mutation of the
source policy or assessed material makes a recorded PASS inapplicable until
reassessment. Shared readiness and per-take candidate qualification consume
this check. Import/edit execution remains separate and may continue while
quality is pending or blocked.

The latest decision changes current coverage/listening only. Captured ingress
bindings and permission flags are unchanged. Affected approved active units
return to MarkerReview without deleting historical reviews or edited material.
The source decision itself cannot approve those units.

## Evidence and commit behavior

The supplied evidence bytes must be nonempty, at most 4 MiB, and match the
expected SHA-256. They are retained as
`source-evidence/<sha256>.quality.txt`; the suffix is a storage convention, not
an assertion that evidence semantics were automatically verified. The existing
package data-file allowlist was not widened.

Failed/cancelled precommit work does not update the caller's project. A retained
unreferenced evidence blob may remain; the command does not delete potentially
shared evidence. A recoverably committed exact generation returns its real
receipt with an uncertain-durability warning instead of inviting duplicate work.
There is no claim of a filesystem sandbox against the machine's owner/root.

Private engineering candidates retain the decision evidence and relevant full
journal history. These candidates and their test packages are **not public
release artifacts**. Treat review notes and source records as restricted data;
final distribution needs the required disclosure/redaction and privacy review.
No real private evidence was uploaded by this implementation.

## CLI

```text
seam_voicebank_cli inspect-source-quality WORKSPACE STRATEGY
seam_voicebank_cli record-source-quality WORKSPACE STRATEGY PROJECT_SHA256 ID REVIEWER UTC COVERAGE LISTENING EVIDENCE EVIDENCE_SHA256
```

CLI outcomes are lowercase `pass`, `blocked`, or `not-assessed`. Inspection is
read-only and returns policy/material/project identities, whether a decision is
recorded, and whether recorded quality passes for current material. Recording
returns the actual committed generation/digest and `releaseEligible: false`.
Use a new ID and freshly inspected project digest for a deliberate reassessment.
Provisioning actors, changing source rights, legacy attribution, native
assessment-entry controls and actual independent qualification remain separate
obligations; these commands do not invent any of them.
