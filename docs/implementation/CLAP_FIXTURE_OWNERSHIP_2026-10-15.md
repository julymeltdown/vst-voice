# Owned temporary fixtures for CLAP host tests

Date: 2026-10-15, Asia/Seoul. Integrating base: `a19a8dc938188b0e2c2f0bbf9e5c6628554b3380`.
Fetch/pull completed first; the checkout was current and clean. **Locally verified
test-hygiene repair; independent review pending, Beta BLOCKED.**

## Failure and change

The earlier independent installed-project/song reviews identified leaked native
fixture directories and deletion based on child stdout without an ownership guard.
[The host tests](../../tests/test_clap_offline_host.py) now launch each native fixture
under a new `TemporaryDirectory`, register cleanup before launching, and supply its
canonical path as `TMPDIR`, `TMP` and `TEMP`. Cleanup runs after successful tests,
assertion failure and subprocess timeout. Returned projects remain available for the
host child until the unittest finishes.

`owned_fixture_project` requires an absolute regular project with the expected
filename, directly inside a correctly named PID/counter fixture directory whose
parent is the launcher-owned root. It refuses project/directory symlinks and
noncanonical or foreign paths. Installed recipe paths must resolve inside that
validated fixture. Before the song test removes its producer and package, it also
requires the expected directory/file to exist and not be symlinks. Recursive final
cleanup comes from the owned `TemporaryDirectory`; stdout does not select its target.

The disposable launcher removes optional `SEAM_SONG_ARTIFACT_ROOT` and
`SEAM_SONG_FIXTURE_OUT` overrides from its child environment so those host regression
runs cannot publish retained fixture artifacts elsewhere. Read-only song source-root
configuration is retained. Standalone native CLI retention features are unchanged.

No shared-temp sweep is added. Earlier fixture directories, original source recordings
and historical evidence dependencies are preserved. This change does not repair the
older verifiers' reliance on original temporary inputs or make relocated resources
self-contained. Native C++ code, CMake, criteria and audio generation are unchanged.

## Executed evidence

Three new mocked ownership methods exercise successful cleanup, child failure,
timeout, foreign stdout and a symlinked project. The initial regression methods
applied to the actual parent implementation produce **five failures**; all three
pass on the initial repair. These use controlled mocked child results, not native
signing, singer-quality or loaded-plugin evidence. The final success method also
checks suppression of the two opt-in external artifact overrides.

The actual parent eight-method host suite runs successfully inside a newly created
observation root, but leaves **five native fixture directories, 51 files and
83,912,602 bytes** there. Four are procedural phrase fixtures and one is the tuned
song fixture. An intermediate repaired eleven-method suite passes and leaves zero.
The final registered `seam_clap_offline_host_tests` CTest entry passes in **10.210s**:
**11 Python methods**, comprising eight existing host/admission methods and three
mocked ownership methods. Its controlled observation root contains **zero directories,
zero files and zero file bytes** when inspected before outer cleanup.

Each observation root is disposable and was created for this unit; its context removes
only that owned root after recording the inventory. The inventoried contents are not
retained replay inputs. No historical source installation is deleted. Five native
fixture invocations inside the host suite repeat existing journey cases; repeated
focused/intermediate runs are not additional cases. The existing 48-note loaded-plugin
song assertions still run, but this unit retains no new PCM or listening result.

This is a Python-only change. No native rebuild is needed or claimed: four runtime
binaries are rehashed before and after execution and retained unchanged. They come
from the prior verified full rebuild, not a newly authenticated build. The unchanged
native runner uses the injected private temp root in these actual macOS executions.
Windows execution remains unverified.

## Retention and remaining work

The [evidence index](CLAP_FIXTURE_OWNERSHIP_2026-10-15.evidence.json) binds **38 artifacts
and eight preserved inputs** under `out/evidence/clap-fixture-ownership-2026-10-15`.
The 7,959-byte manifest has SHA-256
`5b0612233d3fef1b2e3ced7532fc647c975ed5f512dec56ec3074d569dde4ba5`.
The read-only verifier checks retained bytes, source versus Git, original runtime
bindings, receipt streams and inventory observations. It executes neither the native
runtime nor retained launcher code. The initial observation wrapper was reconstructed
by two literal reversals and matches the earlier receipt hashes exactly; it is not
claimed as a contemporaneous source capture. The pre-override-suppression test source
is separately retained, so intermediate and final execution are distinguishable.

Exact `c27d1d6f` publication/revocation review remains confirmed live. This unit, the
host-admission and native-provenance supplements, and later pitch units await their
own verdicts. Durable audit from retained copies, exclusive output publication and
semantic request/timeline integration remain. The 297 internet female originals are
still unqualified; source/performer/identity/public-license disposition, genuine actors,
phonetic coverage, failed-QC ownership and listening remain unresolved. Full classical/
neural/R9 resources, acoustic tolerance ownership and user/DAW/platform/release acceptance
remain required. Alpha stays **0/20 recorded passes**, U45 held, Beta **BLOCKED** and
the full goal ACTIVE. `.github`, README Windows TODO, canonical gates, recordings,
earlier evidence roots and the existing stash are preserved.
