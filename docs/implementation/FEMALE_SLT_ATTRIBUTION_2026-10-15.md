# SLT author attribution carried with derived donor audio

The independent review **APPROVED exact `2b9db572`** for female SLT intake
accuracy, with no findings or required changes. It identified an optional but
material attribution issue: COPYING requires retaining original author names,
while the extracted source/donor packet did not explicitly name them. The prior
plan said that author names were retained; COPYING itself does not name them.
The frozen archive contains Alan W Black and Kevin A. Lenzo's names. This unit
adds their explicit attribution alongside freshly prepared derived WAVs.

The [publisher page](http://festvox.org/cmu_arctic/) and voice-specific
[COPYING](http://festvox.org/cmu_arctic/cmu_arctic/cmu_us_slt_arctic/COPYING)
were fetched again over HTTP. The 2,285-byte license exactly matches the retained
original's SHA-256
`469adc0262e10d84fb9658fa6181c883586415b5692f6bfbc2fde65dfc63f5c5`.
HTTP and local hashes do not authenticate publisher provenance.

The retained archive's `bin/contour_powernormalize` and
`bin/find_powercontours` each credit **Alan W Black and Kevin A. Lenzo**.
Their complete text was read through `tarfile.extractfile`, retained as reference
text and bound to the original archive hash. No archive script was executed.
This establishes the names in the source distribution; it does not establish
the speaker's legal identity or performer/personality clearance.

`prepare_phone_donors.py` now writes `attribution.txt` beside
`license-source.txt`, the captured plan and derived audio. `donors.json` links
the sidecar through `retainedAttributionPath`. The notice retains the caller's
explicit attribution, source URL and existing modification notice and states
that the donors remain unassigned/unapproved. The helper cannot infer author
names or certify license compliance: the plan author must provide accurate
source-specific attribution. Existing input schema and label/PCM semantics are
unchanged.

The actual follow-up plan includes the CMU 2003 notice and both names. It creates
a new packet at
`out/evidence/female-slt-attribution-2026-10-15/attributed/`.
The earlier intake and donor roots remain untouched. All **312 proposal rows
and derived WAV bytes match** the earlier packet; the only new material is the
explicit attribution and its bookkeeping. No original audio was acquired and
the distinct-original census remains **1,429**. Overlap, automatic labels,
missing `axr`, source `jh` mapping, steady singing spans and pitch-layer gaps
are unchanged. No native inspection was repeated for these byte-identical WAVs.

The focused registered CTest intake lane passes **32 Python methods**: the prior
31 plus one attribution/notice test. Existing determinism tests compare the new
sidecar too. The actual preparation CLI completes in **0.094285 s**, with its
three changed source/test/document files and ten bound inputs unchanged before
and after execution. CTest uses the previously rebuilt Release CLI for the
existing native-analysis fixture; this unit changes no C++ source and performs
no new native build. Its CTest receipt does not individually bind that fixture's
executing native binary before and after the run. The evidence is a focused local
check, not a hermetic or authenticated build.

The [evidence index](FEMALE_SLT_ATTRIBUTION_2026-10-15.evidence.json) retains the
complete independent source-intake verdict. Its approval is scoped to exact
`2b9db572`; donor preparation, English producer entry and this attribution
implementation are distinct reviews. The read-only verifier recomputes archive
reference bytes, original license equality, all unchanged donor hashes, source
bindings and run receipts. It executes no native/capture/archive script.

The source review's two citation notes are corrected in the intake dossier:
the vocabulary ID declaration and the actual distinct-review enforcement code.
The remaining performer/owner-policy/producer/distinct-reviewer and human
listening requirements stay open. Attribution creates no production approval.
Usable Alpha remains **0/20**, U45 remains held and External Beta **BLOCKED**.


## 2026-10-15 — Exact fresh SLT attribution review closes packet P2

[The exact attribution verdict](FEMALE_SLT_ATTRIBUTION_REVIEW_2026-10-15.md) APPROVES 4fd8ab42 with no findings or required changes, and closes faf5f420's P2 for the fresh packet only. Original author references, unchanged COPYING/modification notice and all 312 unchanged donors are confirmed by independent read-only checks. The older frozen packet remains inaccurate and must not serve as import/provenance. README source wording is clarified: both names come from archive references; publisher page names Black only. No new runtime, acquisition, performer/owner rights, bank admission or Beta approval. Later code has separate review status; Alpha 0/20, U45 held, Beta BLOCKED.
