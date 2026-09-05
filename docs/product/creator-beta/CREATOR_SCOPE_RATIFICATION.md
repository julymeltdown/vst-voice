# Project SEAM Creator Scope Ratification Contract

**Historical study gate:** This document retains the pre-schema creator study defined by U1 of the earlier Creator Workflow Parity plan. The later [full-scope authority amendment](../FULL_SCOPE_AUTHORITY_AMENDMENT.md) governs implementation and Beta scope.

**Current result:** `NOT_RUN`

**Machine-readable record:** [`creator-scope-ratification.json`](creator-scope-ratification.json)

**Schema:** [`creator-scope-ratification.schema.json`](creator-scope-ratification.schema.json)

The historical study remains `NOT_RUN`, with pending hypotheses and `schema8Authorization: false`. No study result or participant approval is inferred. The user's later full-scope decision supersedes this study's implementation veto and authorizes the coordinated musical schema, complete expressions and all other R1–R20 requirements. The earlier Japanese-only boundary, MIDI deferral, limited expression scope and unchanged-Beta-validator restriction are superseded as recorded in the amendment. The study procedures and ratification rules below describe the earlier study only; they do not narrow or block the new mandatory scope.

## Initial creator segment

Recruit exactly five creators who meet all of these conditions:

- They use UTAU or OpenUtau for Japanese sample-based cover or original-song work.
- They work on macOS or Windows.
- They finish arrangements in a DAW.
- They can bring a real project or approve a rights-safe representative project for the study.
- They consent to the study record and understand the withdrawal and retention policy.

Participant records use pseudonymous IDs such as `creator-01`. Do not place names, email addresses, account handles, raw lyrics, private project paths, or unredacted audio in the canonical JSON record.

## Required prerequisites

Every prerequisite must reach `PASS` and bind its artifact with a repository-relative locator and lowercase SHA-256 before a participant session can be classified `COMPLETED`.

| Prerequisite | Required identity |
|---|---|
| Consent protocol | Approved consent version, collection scope, withdrawal procedure, retention period, and evidence hash. |
| SEAM candidate | Exact installed study build, source commit, artifact hash, platform, and launch evidence. |
| OpenUtau reference | Source commit `8c0dc4007e6e8c8181f3a12c10205671800eeb8b`, executable hash, toolchain/dependency identity, and network-disabled profile with empty plug-in, singer, package, and tool roots. |
| Representative voicebank | Rights-cleared bank ID, version, content hash, installed provenance, and allowed study use. |
| Representative USTX project | Creator-provided or approved rights-safe fixture, exact hash, expected field inventory, and redaction classification. |
| Low-fidelity USTX path | Throwaway study prototype hash and limitations. It may expose the proposed workflow but cannot be presented as production import/export. The current unbound implementation and operator contract are documented in [`USTX_STUDY_BRIDGE.md`](USTX_STUDY_BRIDGE.md). |

If any prerequisite changes, affected sessions are stale and must be rerun. A local source checkout, an unbound OpenUtau profile, or an engineering voice fixture cannot satisfy the prerequisite.

## Session procedure

Run one session per slot, `CSR-001` through `CSR-005`.

1. Confirm target-segment eligibility and consent before recording the screen, audio, timings, or notes.
2. Record the exact platform, SEAM build, OpenUtau profile, voicebank, USTX fixture, and prototype identities.
3. Ask the creator to perform the six tasks without coaching about the intended product decision.
4. For each task, record start and end time, success, observed pain, workaround, blocker severity, notes, and hash-bound raw evidence.
5. Record whether the creator would continue the same song in SEAM after the study.
6. Let the creator withdraw. Withdrawal removes disallowed evidence according to the consent protocol and leaves an auditable `WITHDRAWN` session state without inventing a result.

A session is `COMPLETED` only when consent is `GRANTED`, all six tasks are `COMPLETED`, session evidence exists, the participant matches the segment, and the continuation decision is recorded. A withdrawn, disqualified, blocked, or partially executed session never counts toward the completion threshold.

## Hypotheses and tasks

### CSR-H01 — USTX exchange, R5

Ask the creator to open the representative OpenUtau song through the low-fidelity USTX path, review the stated conversion limitations, save an ordinary `.seam` copy, make one meaningful edit, export the supported USTX subset, and inspect the result in the isolated OpenUtau reference.

Record manual reconstruction, unreported loss, timing disagreement, unsafe reference handling, abandoned work, and whether USTX exchange is necessary for the creator's actual workflow. Do not test MIDI in this study.

### CSR-H02 — Expressive performance, R1-R2

Provide a reference phrase with intentional vibrato and dynamics. Ask the creator to reproduce or adjust the performance using current SEAM, then compare the task with the pinned OpenUtau reference or a non-shipping interaction mockup.

Record inability, workaround steps, audible mismatch, reopen expectations, parameter comprehension, and whether persisted note vibrato and typed dynamics are both required. Do not imply that the mockup already renders or persists the controls.

### CSR-H03 — Creator productivity, R3

Ask the creator to repair a multilingual verse containing repeated lyrics, one pronunciation exception, one overlap, one gap, and one phrase that should become legato.

Record repeated commands, manual navigation, pronunciation workarounds, mistakes, undo expectations, desired batch boundaries, and whether search, phonetic hints, selected-lyric replacement, and normalization materially reduce the task.

### CSR-H04 — Visual clarity, R4

Use the dense-overlap and long-multilingual-text fixture at representative narrow and standard windows and at least 100% and 200% scaling. Ask the creator to identify, select, edit, and explain every overlapping note and its full lyric.

Record clipped or ambiguous text, hidden actions, overlap-selection errors, required zoom or window workarounds, keyboard reachability, and assistive-technology observations when applicable. Existing overlap bands, Unicode fitting, and character modes are being evaluated, not presumed broken.

### CSR-H05 — Style and coverage, R6

Use a trusted bank with at least two styles and two root-pitch layers. Ask the creator to select the intended style, identify a deliberately uncovered pitch, explain the failure, and choose a deliberate recovery action.

Record unintended substitution, unclear bank identity, ambiguous style ownership, missing coverage explanation, and whether a mode-independent Style and Coverage surface is necessary.

### CSR-H06 — Data-only compatibility, R7

Present inert USTX references to an unavailable singer, renderer, resampler, wavtool, dependency, or plug-in. Ask the creator to interpret the explicit incompatibility report and choose how to proceed.

Do not execute or install referenced content. Record whether the creator accepts report-only behavior, requests silent substitution, abandons the task, or can make a deliberate safe recovery choice. Security policy remains data-only even when this hypothesis is rejected; rejection means the workflow or explanation must be revised, not that executable imports become authorized.

## Measurement and severity

Each completed task records:

- completion time in seconds;
- success or failure;
- whether the hypothesized pain was observed;
- the creator's workaround;
- one severity classification;
- raw evidence paths and SHA-256 values.

Severity uses the repository release vocabulary:

- `P0`: data loss, unsafe execution, trust bypass, or inability to complete the core study journey.
- `P1`: a frequent or severe workflow failure that prevents credible Beta use without a workaround acceptable to the target segment.
- `P2`: a material limitation with a viable workaround that may be accepted and documented.
- `P3`: a minor observation that does not affect the scope decision.

Do not convert facilitator opinion into participant evidence. A summary cites the sessions and artifacts that support it.

## Privacy and evidence handling

- Collect the minimum data required for the scope decision.
- Store pseudonymous participant IDs in the canonical record.
- Keep raw audio, screen recordings, private projects, and consent records in their approved restricted evidence location; the JSON stores only safe repository-relative manifests and hashes.
- Redact usernames, home directories, tokens, account data, third-party personal data, and unreleased lyrics from shareable artifacts.
- Honor withdrawal and deletion according to the consent protocol before sealing the study result.
- Product research, QA, and engineering approvals must come from independently attributable reviewers; the facilitator cannot fill every role.

## Ratification rule

The record reaches `PASS` and `schema8Authorization: true` only when all of these conditions hold:

1. All six prerequisites are `PASS` for the same study lineage.
2. Exactly five creators are recruited and recorded in the five fixed slots.
3. At least three eligible creators complete all six tasks.
4. At least three completed creators choose `CONTINUE` for the same song in SEAM.
5. Every hypothesis is `RATIFIED` from at least two completed sessions and hash-bound evidence.
6. No unresolved `P0` or `P1` issue remains.
7. Product research, QA, and engineering independently approve the result with evidence.
8. No plan amendment is required.

A completed task that still records `blockerSeverity: P0` or `P1` is itself unresolved and blocks `PASS`, even when the session's issue list omits it. After remediation, rerun that task and bind the new outcome while retaining the original observation in the resolved issue evidence.

If a hypothesis is rejected or materially different from the plan, set `decision` to `REVISE_PLAN`, keep the gate `BLOCKED`, set `schema8Authorization` to `false`, and record the amended plan locator. Do not retain a feature as a hard predecessor merely because a competitor provides it.

## Fail-closed states

| State | Meaning |
|---|---|
| `NOT_RUN` | No valid study result exists. This is the repository's current state. |
| `BLOCKED` | Prerequisites, sessions, evidence, issue closure, or plan alignment are incomplete or failed. |
| `PASS` | The historical creator study has earned its original ratification result; new full-scope authority remains separate. |

Changing `status` or editing summary counts does not create a pass. The JSON Schema requires completed session bodies, ratified hypotheses, prerequisite evidence, continuation decisions, zero unresolved P0/P1 findings, and three independent approvals.

## Local validation

Validate JSON syntax and the schema itself:

```bash
python3 -m json.tool docs/product/creator-beta/creator-scope-ratification.json >/dev/null
python3 -m json.tool docs/product/creator-beta/creator-scope-ratification.schema.json >/dev/null
```

Validate the record with Draft 2020-12 and format checking:

```bash
python3 -c 'import json; from jsonschema import Draft202012Validator, FormatChecker; s=json.load(open("docs/product/creator-beta/creator-scope-ratification.schema.json")); r=json.load(open("docs/product/creator-beta/creator-scope-ratification.json")); Draft202012Validator.check_schema(s); Draft202012Validator(s, format_checker=FormatChecker()).validate(r)'
```

Run the repository verifier to recompute session, continuation, hypothesis, severity, approval, and evidence-hash relationships:

```bash
python3 scripts/verify_creator_scope_ratification.py --root .
```

Schema validation and the repository verifier prove historical record consistency only. They cannot perform creator sessions or create approval. New implementation authority comes from the full-scope user decision, while actual independent pre-GO creator acceptance remains mandatory under R16.
