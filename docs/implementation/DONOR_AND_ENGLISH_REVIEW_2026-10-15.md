# Exact donor and English producer reviews

The independent reviewer in thread `01a11bad-88f2-7521-849e-c38111dc2197`
approved both exact implementations for engineering scope:

| Commit | Verdict | Findings | Required code changes |
| --- | --- | --- | --- |
| `faf5f420f038ae81b3da7502bce9899ce480a79c` | APPROVE | One P2: inaccurate retained-author claim in the old donor packet | None |
| `ecee9147e3589611cce2df215a0e4e9a8fe341ca` | APPROVE | None | None |

The full responses are retained under
`out/evidence/phone-donor-review-2026-10-15`, bound by the adjacent evidence
index. This record introduces no new native execution, acquisition or acceptance.
The reviewer used Git/file reads and independent calculations; the reviewer's
"rebuild" means independent regeneration of data, not a native build. No project
module, captured script, verifier, CLI, test or build was executed by the reviewer.

## Attribution finding and packet boundary

The old frozen `female-phone-donors-2026-10-15` packet says original author names
are retained, but names neither Alan W Black nor Kevin A. Lenzo. That assertion
is inaccurate. Preserve the old root as historical engineering evidence; **do
not use it as an import/provenance packet or evidence of complete attribution**.

Commit `4fd8ab42` creates a fresh packet with explicit author names, original
COPYING and a modification notice beside unchanged donor audio. Its exact
independent review is now requested. The P2 is **not independently closed** until
that verdict confirms the correction. Copyright attribution does not establish
performer rights, the owner's admission policy or bank approval.

The donor reviewer independently recalculated all 312 spans in both outputs,
their PCM, raw labels, hashes and totals, and replicated the four-source greedy
selection. The result agrees with the retained records: 36.9125 summed seconds,
12.825 unique source seconds, and 300–8,100 frames per cut. These remain speech
donors, unassigned and unreviewed for musical production.

Optional donor notes remain: isolate opaque-column refusal tests from span-bound
failures; exercise the actual label-length bound; retain the source-selection
generator/tie record; broaden verifier checks for span-rule/totals/attribution;
keep notices beside each derived packet. They are not required code changes in
the APPROVE verdict.

## English producer review and remaining integration

The reviewer independently reproduced the full English JSON/CSV and all 3,441
missing assignments, the custom stress-distinguishing hashes, the frozen
93-symbol vocabulary binding and both unchanged Japanese hashes. It confirmed
the retained failed-build/capture/verifier history and stable receipts.

Optional notes remain: bare vowels under `DISTINGUISH_LEXICAL_STRESS` cover only
bare hint symbols, not dictionary vowels with stress digits; native/Python checks
have equivalent refusals but different order/messages; two test sources changed
between the corrected and final builds; preset popup labels conflate assignments
and alternate takes and omit the English count. The exact verifier command is:

```sh
python3 out/evidence/english-producer-workspace-2026-10-15/verify.py --source-commit ecee9147e3589611cce2df215a0e4e9a8fe341ca
```

The later bank-core commit `d5ecc1f9` has separate focused evidence and pending
independent review. Producer-to-manifest policy propagation remains open. Neither
verdict grants acceptance to later code or imports the 1,429 original female WAVs
or 312 derived donors. Real actors, owner policy, singing/pitch/style coverage,
human listening, classical/neural and multilingual resources, R9, physical/DAW
evidence, acoustic tolerances and full candidate checks remain open. Usable Alpha
is 0/20 recorded passes, U45 held, External Beta BLOCKED; goal ACTIVE.
