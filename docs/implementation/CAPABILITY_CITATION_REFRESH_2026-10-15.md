# Capability refusal citations — 2026-10-15

Focused PASS; independent review pending; full Release suite after repairs NOT_RUN.
Source base: `317268f72ccfc873019b33811d65f88bd95933d5`.

The full baseline registered external-Beta Python discovery twice; both lanes
failed the same three StyleBlend carrier subcases in `test_capability_matrix.py`.
A focused discovery of that file reproduced the three failures among five tests
in 0.044553 seconds. Native code still raises all three refusals; the report's
citations had drifted to lines six before their messages.

Only `tools/external_beta/capability_matrix.py` changes. Its six neural expression
return citations now identify exact Unsupported returns at render_snapshot.cpp
lines 820, 824, 829, 832, 835 and 838. Literal StyleBlend refusals now distinguish
return from message lines:

| Carrier | Unsupported return | Message |
| --- | ---: | ---: |
| Neural | 816 | 817 |
| Procedural | 740 | 741 |
| Sample | 1045 | 1046 |

Comments now describe refusal paths under their actual guards, including neural
breathiness when the acoustic graph lacks that conditioning control. They do not
assert universal backend limitations. No refusal text, carrier, capability,
positive-coverage flag, unmodelled contract kind or native runtime behavior changes.
The report remains REFUSALS_ONLY; absent cells do not claim support.

Final focused discovery passed all **five tests** in **0.044673 seconds**. A
separate source check verified all **11 exact Unsupported return lines**, beyond
the existing test's nearby-source windows. Native sources and the test file are
byte-identical to the source base. No native rebuild/test was needed for this
Python citation-only unit; a full dependent rebuild and suite are next.

Root `out/evidence/capability-citation-refresh-2026-10-15/` retains **18 artifacts,
10 preserved pins**, baseline/final sources, source-bound failed/passing receipts,
generated report and a read-only verifier. Manifest: 5,302 bytes, SHA-256
`755499ef6908158fb7a0d74cd2034884d126d6069531e2223e1261549d900222`.
The verifier passed hash/Git bindings, failure/pass counts, exact return sites,
message windows and unchanged negative-only report semantics. Interpreter and
transitive runtime identities are not bound; this is selected local evidence.

The duplicate CTest registrations remain verification-cost debt. Refreshing
citations closes this stale report, not a missing positive capability matrix or
real released resource. Alpha remains 0/20, U45 HELD, Beta BLOCKED and release
eligibility false. Audio admission, .github and Windows README TODO are unchanged.
