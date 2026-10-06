# Engineering soak supervision

`tools/external_beta/soak_supervisor.py` prepares the independent liveness component
required by SEAM-BETA-P1-05. Full-scope U47 still requires exact signed-installed
30/120-minute physical workloads and independent review. This component does not
close that requirement or promote a gate.

Call `supervise_soak` from a process distinct from both caller-owned product and
collector children. Pass their child handles, a dedicated heartbeat pipe reader,
a dedicated `finished_fd` pipe reader and a pre-opened `final_sample_fd` regular file;
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

Endpoint protocol `durable-final-sample-v1` requires one newline-delimited JSON
FINISHED frame (at most 1024 bytes) plus its matching final-sample file (at most
4096 bytes). `record_id` identifies this one session and must match in both artifacts.
The frame has exactly `type: FINISHED`, `recordId`, `durationSeconds`,
`heartbeatSequence`, `sampleBytes` and `sampleSha256`. The file has exactly
`schemaVersion: 1`, `recordType: external-beta-final-soak-sample`, `recordId`,
`durationSeconds`, `heartbeatSequence` and `sample`. The sample's finite
`elapsedSeconds` must cover the declared duration and fit within the one-second
endpoint acknowledgement budget. The frame hash and length cover the exact file bytes.

The caller opens a fresh empty regular file before supervision and retains a
separate write descriptor for the collector. `commit_final_sample` handles partial
file writes, verifies final size and successfully calls `os.fsync` before emitting
FINISHED. This defines the commit boundary; an in-memory callback or a Python
buffer flush alone is insufficient. It leaves partial bytes on failure and sends
no successful acknowledgement after a failed write/fsync. Emit the final heartbeat
only after that helper succeeds.
The helper retains caller ownership of its write descriptors.

FINISHED may arrive before the corresponding heartbeat has been read on the other
pipe. The supervisor joins the exact sequence, reads/hashes/parses the retained file,
and rechecks its bytes immediately before completion. It continues bounded FINISHED
reads until completion and rejects later duplicate/trailing data or EOF, including
while the final heartbeat is pending. Missing, partial, replayed,
oversized, early, wrong-session, wrong-duration or wrong-sequence acknowledgement,
missing/substituted/short final sample, or EOF prevents completion. Both children
and the heartbeat pipe must stay alive during the bounded endpoint exchange.
No keepalive timer or manufactured replacement samples are permitted.

Completion requires the full independent clock span and the committed endpoint.
One additional second is available solely for the final exchange; the declared
1800/7200 duration and existing heartbeat/observation budgets remain recorded and
enforced. Completion is refused at the endpoint deadline if the exchange is incomplete.
Sleep and cadence validation share one next-wake target, shortened at the declared
duration and final deadline; a valid non-divisor cadence or scheduling drift must
not turn those shortened boundary steps into early-polling errors.
Receipt schema version 2 records the protocol, budget, parsed frame, receipt time
and parsed final sample. Old version-1 receipts remain unchanged and do not gain
endpoint evidence. Missing new API descriptors is an input error, with no ownership
transfer or clock-only compatibility path.

Either child's exit, missing/stale/malformed/implausibly fast heartbeat, pipe closure,
clock reversal, early polling, observation gap, or observation error prevents completion.
After observation starts, ownership of the children, both pipe readers and sample file transfers to the
supervisor: it terminates and reaps both children on success or failure, uses at most
two seconds for each terminate/kill wait, and attempts all three descriptor closes independently. Cleanup failure also
refuses completion. Permission-denied cleanup is reported without another signal route.
Invalid configuration leaves ownership with the caller.
Interruptions are retained while bounded cleanup stages for both children and the
reader are attempted independently. The original interruption is then propagated
with a failed receipt, rather than masking cancellation or skipping later cleanup.

Receipts bind the declared record ID, installed-app tree digest and workload digest,
and always carry `evidenceScope: engineering` and `releaseEligible: false`. Caller-supplied
identities are not an attestation of installed bytes. Heartbeats and child exit checks
establish observed liveness, not application progress, audio quality or metric authenticity.
The endpoint checks enforce the writer's fsync-before-FINISHED source contract and
retained-byte binding. They do not authenticate an untrusted writer's fsync claim,
prove power-loss survival, validate the full metric series, or establish physical authority.

`tools/external_beta/soak_session.py` adds the owned engineering adapter:
`run_engineering_soak_session(product_argv, identity_manifest, output_dir)`. It
launches one product child from an argument list and one private collector worker;
it has no shell or attach-to-PID mode. The manifest requires exact 1800/7200 duration,
declared installed-tree/workload/machine-profile SHA-256 digests and a machine-profile
ID. Existing source/build/bank/project identities can be retained when supplied.
The adapter creates a fresh session ID, exclusively creates the output directory and
freezes canonical manifest bytes before launching either child. Caller-supplied
session or authority fields are refused. Existing packet directories remain untouched.

Bounded READY/GO frames bind the session ID, manifest digest and product PID. Each
startup wait is at most five seconds, with frames at most 1024 bytes. The adapter
owns both child handles and all descriptors until the supervisor enters observation;
its first clock call sends GO with that same monotonic epoch and transfers the three
read descriptors. Invalid supervisor inputs and earlier startup failures retain
adapter ownership. The approved stop/reap rules apply in either case. The adapter
closes its remaining writer/control descriptors independently; denied cleanup has
no retry or alternate signal route and prevents a sealed packet.

The collector's optional keyword-only `on_sample` callback receives a copy of each
formed sample. A callback exception aborts collection. `start_monotonic` supplies
the fixed GO epoch and `maximum_lateness_seconds` bounds the work budget; existing
return and measurement shapes are retained. Sampling targets are absolute
`GO + k * interval`, including zero and the exact endpoint. Sleep covers only the
remaining time to the next target. Actual monotonic sample timestamps are retained;
delayed wakes do not rebase targets or backdate samples. The session uses one-second
targets and rejects a missed target or measurement/persistence overrun. No catch-up
burst, skipped sequence or timer heartbeat is allowed.

Each bounded raw row binds session ID, manifest hash, product PID, sequence and
intended target to the measured sample. Complete append and successful raw-file
fsync precede its heartbeat. The final row additionally uses `commit_final_sample`
and exclusively persists the worker result before the final heartbeat. All that
work must finish before `GO + duration + 1`; a late final helper cannot justify a
final heartbeat even when partial snapshot/FINISHED bytes already exist. The adapter
uses the supported 0.1-second supervisor poll so nonzero final-write latency can be
observed inside the unchanged endpoint deadline. No supervisor limit is relaxed.
After FINISHED, the worker keeps protocol writers open without more heartbeats until
supervisor cleanup; a duration-plus-nine-second lease bounds a missing cleanup.

After child cleanup, the adapter exclusively retains the supervisor receipt in
`supervision.json`, including an exception-attached failed receipt. It never changes
that receipt to match adapter validation. Completion joins the receipt's identities,
duration and child PIDs to the manifest, requires contiguous raw rows and their
timing/bindings, and matches the acknowledged final snapshot to the final raw sample
and its exact byte hash/length. A mismatch produces a failed adapter result while
preserving the original receipt. Partial files and worker/session error details are
retained on failure; original interruptions propagate after bounded cleanup.

Packet hashes are computed after cleanup, when neither owned child retains a writer.
The final index binds existing manifest, raw samples, endpoint, worker details,
supervision and error files by SHA-256 and byte length. It is exclusively written and
fsynced as `packet-index.pending.json`, then linked without replacement as
`packet-index.json`; the pending name alone never confirms completion. Failed cleanup
or a denied hash action prevents sealing. A failed packet index can describe retained
failure evidence, but only an index with `status: COMPLETE` reports engineering
completion. This is a byte-consistency contract, not an installed-byte attestation,
power-loss guarantee or proof of all metric channels. Every adapter result and index
remain `evidenceScope: engineering`, `releaseEligible: false`.

Controlled regressions run with:

```sh
python3 -m unittest tests.external_beta.test_soak_supervisor tests.external_beta.test_soak_endpoint tests.external_beta.test_soak_session -v
```

Simulated-clock endpoint tests are protocol checks. A short controlled-child timeout
checks real stop/reap behavior. Session tests use controlled measurements, fake child
handles and clocks, and instrument synchronization for full simulated spans; a short
temporary-file test exercises actual raw-file writes/fsync. Neither is physical soak
or live RSS acceptance. The installed workload driver and existing saved-record
product-soak gate are unchanged. Physical/release gate integration requires a later
independent review. SEAM-BETA-P1-05 remains open.
