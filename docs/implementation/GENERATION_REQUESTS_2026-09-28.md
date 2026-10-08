# Generation requests and their outcomes (U21)

Generated material reaches a producer workspace only through a submitted generation request and the
canonical repository writer. This note records the contract implemented on 2026-09-28. Everything it was
tested with is a synthetic engineering fixture: no human listening, review, singer qualification or
release evidence exists, and a request or outcome record is never an approval.

## The request registry

The producer workspace keeps its requests under `generation-requests/<requestId>/`, written by
`GenerationRequestRegistry` in `libs/seam-voicebank-production`:

- `request.json` (create-new, never rewritten): the request ID (the campaign definition's SHA-256), the exact
  producer generation and hash it expects, the language, the recipe identity, the budgets (jobs, frames,
  retained bytes, per-batch jobs and frames) and every job's ID, take, style, coverage key, pitch layer,
  frame count and batch. A definition locator is kept as a hint, not identity.
- `terminal.json` (create-new, at most one): COMPLETED, STALE or BUDGET_EXHAUSTED, the producer generation
  and hash it was observed against, the batches proven collected, retained bytes, operator and time.

Submission is admitted only while the producer is at the request's expected generation, for assignments
whose identity matches each job and whose takes do not exist yet. Every read re-verifies both records
against the producer history. A terminal record must follow from that history:

| Outcome | Required history |
|---|---|
| COMPLETED | one `import-generated-batch` generation per batch after the expected generation, each introducing exactly that batch's takes in MarkerReview or Rejected, ending at the final collection |
| STALE | the proven collections, then a producer beyond them |
| BUDGET_EXHAUSTED | the proven collections and retained bytes above the admitted budget |

Cancellation is not terminal: retained work stays resumable. Requests do not use the producer journal
itself, because a producer generation per submission or outcome would advance the producer and make
the request's own expectation stale; the only producer mutation remains the existing batch collection.

## One advancement operation for CLI and Studio

`advanceGenerationRequest` (authoring runtime) submits a fresh campaign, then completes at most one
bounded batch: prepare, render (reusing verified output), collect. Batch collection, completion,
cancellation, staleness and an exhausted output budget are returned as outcomes with the producer state
they left; only a collection changes the producer. Terminal outcomes are recorded before they are
reported and returned again, without work, on every retry. A take ID already held by another result (a
manual candidate or another request) is staleness, never a collection. A campaign advanced before this
registry existed keeps working unregistered and reports the same outcomes without records.
`advanceGenerationCampaign` remains as an adapter that neither submits nor records and returns errors
for cancellation, staleness and budget exhaustion. A completed campaign now stays completed when later
review or edits change the producer; it no longer reports an error.

CLI (the same operation):

```text
seam_voicebank_cli submit-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC
seam_voicebank_cli advance-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC
seam_voicebank_cli list-generation-requests WORKSPACE
seam_voicebank_cli inspect-generation-request WORKSPACE REQUEST_ID
```

`advance-generation-campaign` prints the outcome, the producer generation and hash, whether the request is
registered and whether the outcome is recorded. It exits 0 for a collected batch or completion, 3 for
STALE, 4 for BUDGET_EXHAUSTED and 128+N when signal N cancelled it. The Python producer tools mirror the
registry: `python3 -m tools.external_beta.voicebank_production inspect-generation-requests --workspace W`
prints the same records as `list-generation-requests`, and `validate-workspace --draft` includes them.

## The byte budget

A campaign request's `maximumBytes` is the plan's admitted `maximumEstimatedBytes`: the allowance for what
the request itself retains. `measureCampaignRequestStorage` counts exactly that: the logical bytes of
`campaign.json` and of the request's own `batch-0` to `batch-(batchCount - 1)` directories, whatever they
hold (job packages, staged audio, receipts or stray files). It does not count the `preflight` directory,
which is bounded by its own phrase and frame admission, or any other file beside the campaign, which is not
the request's output. Links and special files inside what is counted are refused, not skipped. The
advancement measures before preparing a batch, after rendering it and after collecting it; above the
allowance the request ends BUDGET_EXHAUSTED with the measured size and the producer unchanged.

## Evidence and limits

`seam_voice_generation_workflow_tests` covers resume without duplicate takes or approval, staleness from a
changed recipe, a manual candidate, an inventory change and a bare producer save, cancellation and budget
exhaustion keeping the previous active state, registry admission, and review and edit of a generated take
through the recorded-material lifecycle. `tests/production/test_generation_request_mirror.py` drives the
real CLI to a completed, an exhausted and a stale request and refuses eight forged records in both readers.

Not covered: the single-job and batch commands (`prepare-generation`, `run-generation-batch`,
`import-generated-batch`) do not submit requests; they stay guarded only by their expectations. Studio uses
the shared operation but has no request list yet. Terminal records are integrity records, not signatures.

Integration correction (2026-10-08): the retained-entry ceiling includes each batch directory itself as well as its children and campaign.json. A two-entry ceiling rejects a definition+batch+child tree; a three-entry ceiling accepts it. This was reproduced failing before the integration fix.
