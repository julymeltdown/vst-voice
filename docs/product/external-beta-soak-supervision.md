# Engineering soak supervision

`tools/external_beta/soak_supervisor.py` prepares the independent liveness component
required by SEAM-BETA-P1-05. Full-scope U47 still requires exact signed-installed
30/120-minute physical workloads and independent review. This component does not
close that requirement or promote a gate.

Call `supervise_soak` from a process distinct from both caller-owned product and
collector children. Pass their child handles and a dedicated heartbeat pipe reader;
it does not attach to arbitrary PIDs or query RSS. The collector protocol is one
newline-delimited positive integer per second, starting at one without gaps or replay.
Sequence progress is also checked against supervisor elapsed time, with exactly one
second of startup/receipt lateness allowed: one canonical sampling interval. This
tolerance is recorded in the receipt; five-second heartbeat cadence is refused.
Receipt timestamps come from the supervisor's monotonic clock. Required duration is
exactly 1800 or 7200 seconds, poll cadence 0.1-1 second, and maximum heartbeat or
observation gap 1-5 seconds. Frame size, read size and observation count are bounded.
The first observation must occur within one poll interval of start; a delayed first
observation cannot replace the full observed span or manufacture a completed endpoint.

Either child's exit, missing/stale/malformed/implausibly fast heartbeat, pipe closure,
clock reversal, early polling, observation gap, or observation error prevents completion.
After observation starts, ownership of the children and reader transfers to the
supervisor: it terminates and reaps both children on success or failure, uses at most
two seconds for each terminate/kill wait, and closes the reader. Cleanup failure also
refuses completion. Permission-denied cleanup is reported without another signal route.
Invalid configuration leaves ownership with the caller.
Interruptions are retained while bounded cleanup stages for both children and the
reader are attempted independently. The original interruption is then propagated
with a failed receipt, rather than masking cancellation or skipping later cleanup.

Receipts bind the declared record ID, installed-app tree digest and workload digest,
and always carry `evidenceScope: engineering` and `releaseEligible: false`. Caller-supplied
identities are not an attestation of installed bytes. Heartbeats and child exit checks
establish observed liveness, not application progress, audio quality or metric authenticity.

Controlled regressions run with:

```sh
python3 -m unittest tests.external_beta.test_soak_supervisor -v
```

Simulated-clock endpoint tests are protocol checks. A short controlled-child timeout
checks real stop/reap behavior; neither is physical soak or RSS acceptance. A later
reviewed unit must connect the installed runner/collector heartbeat writer, bind the
remaining candidate/session identities, preserve raw receipts, and require their
semantic validation in the existing product-soak gate. SEAM-BETA-P1-05 remains open.
