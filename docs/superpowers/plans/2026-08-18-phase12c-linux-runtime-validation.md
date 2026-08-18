# Phase 12C Linux Runtime Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 공식 CLAP validator와 Linux 336-case matrix, 정확히 2시간 soak, GUI 1,000회, cancellation 10,000회, realtime allocation 및 sanitizer 검증을 실행하고 재현 가능한 증적을 저장한다.

**Architecture:** 각 검증은 독립 실행 파일로 구현하고 공통 `ValidationContext`와 JSON evidence writer를 사용한다. 빠른 CI smoke와 release-acceptance full run을 별도 preset으로 구분하되, smoke 결과는 Phase 12C completion을 만족하지 않는다. 최종 aggregator는 모든 필수 evidence의 commit, binary SHA-256, tool version, duration과 outcome을 검증한다.

**Tech Stack:** C++20, Python 3, CMake/CTest, official `free-audio/clap-validator` 0.4.1, ASan/UBSan/TSan, Linux `/proc` resource counters, X11/Xvfb.

**Spec:** `docs/superpowers/specs/2026-08-18-phase12c-runtime-validation-design.md`

## Global Constraints

- Full matrix는 정확히 336 base cases다.
- Full soak는 정확히 7,200초다.
- GUI lifecycle은 정확히 1,000 cycles다.
- Cancellation storm은 최소 10,000 revisions다.
- Official validator unavailable/build failure/timeout/non-zero는 PASS가 아니다.
- 모든 evidence는 commit, binary SHA-256, compiler, OS, architecture, start/end timestamp를 기록한다.
- Full evidence가 없으면 Phase 12C engineering completion은 false다.

---

## File Structure

```text
libs/seam-validation/
├── include/seam/validation/
│   ├── evidence.hpp
│   ├── process_case.hpp
│   └── resource_probe.hpp
└── src/
    ├── evidence.cpp
    ├── process_case.cpp
    └── resource_probe_linux.cpp

apps/
├── seam-phase12c-matrix/
├── seam-phase12c-soak/
├── seam-phase12c-gui-lifecycle/
├── seam-phase12c-cancellation-storm/
└── seam-phase12c-realtime-probe/

scripts/
├── acquire_clap_validator.py
├── run_phase12c_linux_validation.py
├── verify_phase12c_evidence.py
└── verify_phase12c_contracts.py
```

---

### Task 1: Add the common validation evidence model

**Files:**
- Create: `libs/seam-validation/include/seam/validation/evidence.hpp`
- Create: `libs/seam-validation/src/evidence.cpp`
- Create: `libs/seam-validation/include/seam/validation/resource_probe.hpp`
- Create: `libs/seam-validation/src/resource_probe_linux.cpp`
- Create: `tests/test_phase12c_evidence.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct EvidenceIdentity final {
  std::string commit;
  std::string binarySha256;
  std::string buildType;
  std::string compiler;
  std::string operatingSystem;
  std::string architecture;
  std::string startedAtUtc;
  std::string finishedAtUtc;
};

enum class ValidationOutcome { NotRun, Blocked, Fail, Pass };

struct ResourceSnapshot final {
  std::uint64_t residentBytes{0U};
  std::uint64_t threadCount{0U};
  std::uint64_t fileDescriptorCount{0U};
};

[[nodiscard]] core::Result<void> writeEvidenceJson(
    const std::filesystem::path& path,
    const EvidenceIdentity& identity,
    ValidationOutcome outcome,
    const formats::JsonValue& payload);
[[nodiscard]] ResourceSnapshot captureLinuxResources();
```

- [ ] **Step 1: Write round-trip and required-field tests**

Create evidence, write JSON, parse with the existing JSON parser, and assert every identity field and explicit `outcome` exists. Reject an empty commit or binary hash.

- [ ] **Step 2: Write Linux resource snapshot test**

Assert resident bytes > 0, thread count >= 1, and file descriptor count >= 3 in the test process.

- [ ] **Step 3: Implement and run tests**

Use `/proc/self/status`, `/proc/self/task`, and `/proc/self/fd`. If `/proc` is unavailable, return zero plus a `BLOCKED` diagnostic in tools; do not fabricate counts.

- [ ] **Step 4: Commit**

```bash
git add libs/seam-validation tests/test_phase12c_evidence.cpp CMakeLists.txt
git commit -m "test: add Phase 12C validation evidence model"
```

---

### Task 2: Pin and execute official `clap-validator` 0.4.1

**Files:**
- Create: `scripts/acquire_clap_validator.py`
- Modify: `scripts/run_clap_validator.sh`
- Create: `scripts/verify_clap_validator_evidence.py`
- Modify: `third_party/manifest.yml`
- Create: `docs/phase12c/CLAP_VALIDATOR.md`
- Modify: `.github/workflows/ci.yml` or the active Linux workflow

**Interfaces:**
- Produces:

```text
out/phase12c/clap-validator/validator.log
out/phase12c/clap-validator/result.json
out/phase12c/clap-validator/tool-source.json
```

- [ ] **Step 1: Resolve tag `0.4.1` to an exact commit**

`acquire_clap_validator.py` must query the official repository, verify tag `0.4.1`, and require exact commit `152b9823e992d782c5c1fd33bca0295478b919aa`. It runs `git rev-parse HEAD`, stores the full commit in `tool-source.json`, and refuses an existing checkout at any other revision.

- [ ] **Step 2: Build the release validator and record its SHA-256**

Run Cargo with `--locked --release`. Record Rust version, Cargo version, validator binary SHA-256 and source commit.

- [ ] **Step 3: Validate the release `ProjectSEAMEditor.clap`**

Run the exact command already used by the repository's pinned validator workflow:

```bash
external/clap-validator/target/release/clap-validator \
  validate build/phase12c-release/ProjectSEAMEditor.clap \
  --only-failed \
  > out/phase12c/clap-validator/validator.log 2>&1
```

Capture the unmodified log, exit code, start/end timestamps and duration.

- [ ] **Step 4: Add a negative evidence test**

Feed the verifier a result with `toolVersion = 0.4.0`, empty raw log, or exit code 1. Assert the verifier exits non-zero.

- [ ] **Step 5: Enforce PASS semantics**

Only these conditions produce `PASS`:

```text
exact source tag and commit verified
validator binary built successfully
raw log exists and is non-empty
release plugin SHA-256 matches result JSON
validator exit code == 0
timeout == false
```

Missing network or Cargo produces `BLOCKED`, which fails Phase 12C completion.

- [ ] **Step 6: Commit**

```bash
git add scripts third_party/manifest.yml docs/phase12c .github/workflows
git commit -m "test: pin and run official CLAP validator"
```

---

### Task 3: Implement the 336-case process matrix

**Files:**
- Create: `libs/seam-validation/include/seam/validation/process_case.hpp`
- Create: `libs/seam-validation/src/process_case.cpp`
- Create: `apps/seam-phase12c-matrix/main.cpp`
- Create: `tests/test_phase12c_matrix_definition.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
enum class ValidationRenderMode { Realtime, Offline };

struct ProcessCase final {
  std::uint32_t sampleRate;
  std::uint32_t bufferFrames;
  std::uint8_t channels;
  ValidationRenderMode mode;
};

[[nodiscard]] std::vector<ProcessCase> mandatoryProcessCases();
```

- [ ] **Step 1: Write the exact cardinality test**

Assert:

```cpp
const auto cases = mandatoryProcessCases();
if (cases.size() != 336U) return 1;
```

Also assert every combination of `{44100,48000,88200,96000,176400,192000}` × `{16,32,64,128,256,512,1024}` × `{1,2,4,8}` × `{Realtime,Offline}` occurs exactly once.

- [ ] **Step 2: Implement one-case host harness**

For each case:

```text
load release CLAP module
select output config
activate with sample rate and max buffer
set render mode
start processing
play/stop/seek
CLAP note-on/note-off
MIDI pitch bend
CLAP tuning expression
inactive state save/load
stop/deactivate/destroy
```

- [ ] **Step 3: Define per-case assertions**

Each result records:

```text
finite output
exact output channel count
no write past guard region
stopped transport silence
note event energy > threshold
state round-trip hash
invalid diagnostic count
output checksum
```

Allocate audio buffers with 64-byte guard regions and verify guards after every process call.

- [ ] **Step 4: Implement focused submatrix events**

Add deterministic focused cases for loop, tempo change, articulation fallback and voice stealing. Store these outside the base-case count so `baseCaseCount` remains 336.

- [ ] **Step 5: Run the full matrix**

```bash
./build/release/seam_phase12c_matrix \
  --plugin ./build/release/ProjectSEAMEditor.clap \
  --output out/phase12c/process-matrix.json
```

Expected: `passedCases == 336`, `failedCases == 0`, focused cases all pass.

- [ ] **Step 6: Commit**

```bash
git add libs/seam-validation apps/seam-phase12c-matrix tests/test_phase12c_matrix_definition.cpp CMakeLists.txt
git commit -m "test: add Phase 12C process matrix"
```

---

### Task 4: Implement the realtime allocation probe

**Files:**
- Create: `apps/seam-phase12c-realtime-probe/main.cpp`
- Create: `tests/test_phase12c_realtime_contract.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `phase12c-realtime-probe.json` with allocation, lock, I/O and callback counters.

- [ ] **Step 1: Add test-only global allocation hooks**

Override `operator new/delete`, `malloc/free` wrappers where supported, and gate counting with a thread-local `insideAudioCallback` flag. Warm up before activation and reset counters immediately before process calls.

- [ ] **Step 2: Add forbidden-operation probes**

Link test-only wrappers around project file I/O and logger entry points used by Project SEAM. Assert none are called while `insideAudioCallback` is true.

- [ ] **Step 3: Exercise live note, expression, preview and transport together**

Process 100,000 blocks using buffer sizes 16, 64, 257 and 1024. Include note bursts, state already loaded, host seek and loop.

- [ ] **Step 4: Enforce acceptance**

```text
allocationsAfterActivation == 0
freesAfterActivation == 0
fileIoCalls == 0
loggerCalls == 0
nanOrInf == 0
bufferOverrun == 0
```

- [ ] **Step 5: Commit**

```bash
git add apps/seam-phase12c-realtime-probe tests/test_phase12c_realtime_contract.cpp CMakeLists.txt
git commit -m "test: add realtime audio contract probe"
```

---

### Task 5: Implement the 1,000-cycle GUI lifecycle runner

**Files:**
- Create: `apps/seam-phase12c-gui-lifecycle/main.cpp`
- Modify: `apps/seam-clap-editor-host/main.cpp` to expose reusable host helpers or move them into `libs/seam-validation`.
- Create: `tests/test_phase12c_gui_cycle_definition.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `phase12c-gui-lifecycle.json`.

- [ ] **Step 1: Encode the exact lifecycle sequence**

```text
create
set_parent
set_size
show
10 timer/update callbacks
hide
destroy
```

Alternate sizes `640×420`, `920×620`, `1280×800`, and include Korean/Japanese lyric state on every cycle.

- [ ] **Step 2: Add counter snapshots**

Capture resident bytes, threads, file descriptors and X11 child count before warm-up, after warm-up, every 100 cycles, and after cycle 1,000.

- [ ] **Step 3: Detect stale callbacks**

After `destroy`, call the host timer dispatcher with the previous timer ID and assert the plugin does not access the destroyed view or repaint callback.

- [ ] **Step 4: Run exactly 1,000 cycles under Xvfb**

```bash
xvfb-run -a ./build/release/seam_phase12c_gui_lifecycle \
  --plugin ./build/release/ProjectSEAMEditor.clap \
  --cycles 1000 \
  --output out/phase12c/gui-lifecycle.json
```

Expected: cycles completed 1,000, crashes 0, stale callbacks 0, resource counts return to bounded steady-state.

- [ ] **Step 5: Commit**

```bash
git add apps/seam-phase12c-gui-lifecycle libs/seam-validation tests/test_phase12c_gui_cycle_definition.cpp CMakeLists.txt
git commit -m "test: add 1000-cycle CLAP GUI lifecycle validation"
```

---

### Task 6: Implement the 10,000-revision cancellation storm

**Files:**
- Create: `apps/seam-phase12c-cancellation-storm/main.cpp`
- Create: `tests/test_phase12c_cancellation_definition.cpp`
- Modify: `libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp` only if a read-only diagnostic accessor is needed.
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `phase12c-cancellation-storm.json`.

- [ ] **Step 1: Generate deterministic revisions**

Starting from one project, apply 10,000 seam/pitch/note edits using a fixed xorshift seed. Submit a render after every revision without waiting.

- [ ] **Step 2: Assert latest-only publication**

Wait for idle and assert:

```text
latest published revision == 10000 + initial revision
published stale revisions == 0
final PCM hash == clean single render hash of final project
pending queue bounded at one request
cache entry for stale revision absent or quarantined
```

- [ ] **Step 3: Run under TSan**

Expected: no data race, deadlock or stale publication.

- [ ] **Step 4: Commit**

```bash
git add apps/seam-phase12c-cancellation-storm tests/test_phase12c_cancellation_definition.cpp CMakeLists.txt
git commit -m "test: add 10000-revision render cancellation storm"
```

---

### Task 7: Implement the exact two-hour soak runner

**Files:**
- Create: `apps/seam-phase12c-soak/main.cpp`
- Create: `scripts/run_phase12c_soak.sh`
- Create: `tests/test_phase12c_soak_configuration.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```text
phase12c-soak-summary.json
phase12c-soak-timeseries.csv
phase12c-soak.log
```

- [ ] **Step 1: Define full and smoke profiles**

```text
full duration  = 7200 seconds
smoke duration = 60 seconds
sample period  = 5 seconds
```

Only `profile=full` may set `acceptanceSatisfied=true`.

- [ ] **Step 2: Implement deterministic operations**

Every second choose one operation from a fixed schedule:

```text
play/stop/seek/loop
note edit
phoneme/unit/seam edit
render cancellation burst
Voicebank exact relink
Voicebank mutation rejection
inactive state save/load
live note/expression burst
33-note voice stealing
GUI show/hide
```

- [ ] **Step 3: Record resource and audio counters**

CSV columns:

```text
elapsedSeconds,residentBytes,threads,fileDescriptors,
underruns,nanInf,stalePublications,cacheHits,cacheMisses,
activeVoices,renderSubmitted,renderCompleted,renderCancelled
```

- [ ] **Step 4: Define memory-growth acceptance**

Ignore the first 10 minutes as warm-up. Fit a linear trend over the remaining resident-memory samples. Fail when slope exceeds 1 MiB/hour or the last 10-minute median exceeds the first post-warm-up 10-minute median by more than 64 MiB.

- [ ] **Step 5: Run the exact full soak**

```bash
./build/release/seam_phase12c_soak \
  --plugin ./build/release/ProjectSEAMEditor.clap \
  --profile full \
  --duration-seconds 7200 \
  --output out/phase12c
```

Expected: elapsed >= 7,200 seconds, acceptance satisfied, underrun/NaN/stale counts zero, bounded resource growth.

- [ ] **Step 6: Commit**

```bash
git add apps/seam-phase12c-soak scripts/run_phase12c_soak.sh tests/test_phase12c_soak_configuration.cpp CMakeLists.txt
git commit -m "test: add exact two-hour Phase 12C soak"
```

---

### Task 8: Add sanitizer suites and evidence aggregation

**Files:**
- Create: `scripts/run_phase12c_linux_validation.py`
- Create: `scripts/verify_phase12c_evidence.py`
- Create: `scripts/verify_phase12c_contracts.py`
- Create: `.github/workflows/phase12c-linux.yml`
- Create: `docs/phase12c/ACCEPTANCE.md`
- Create: `docs/phase12c/EVIDENCE.md`
- Create: `docs/phase12c/IMPLEMENTATION_REPORT.md`
- Modify: `docs/RELEASE_READINESS.md`
- Modify: `docs/RELEASE_READINESS_KO.md`

**Interfaces:**
- Produces final `phase12c-verification-matrix.json` and fails unless all Linux mandatory checks are PASS.

- [ ] **Step 1: Define required evidence keys**

```text
clapValidator
processMatrix
realtimeProbe
guiLifecycle
cancellationStorm
twoHourSoak
asan
ubsan
tsan
masterOnly
licenseAudit
mandatoryFutureValidationConnected
```

- [ ] **Step 2: Add ASan/UBSan focused run**

Build and run live engine, matrix smoke, GUI lifecycle smoke and cancellation storm smoke. Store raw logs and exact test commands.

- [ ] **Step 3: Add TSan focused run**

Run resource publication, live engine concurrency, cancellation storm, GUI timer lifecycle and state/load concurrency. A runtime unsupported by TSan is `BLOCKED`, not PASS.

- [ ] **Step 4: Add final verifier**

The verifier checks every file exists, parses, references the current commit and plugin SHA-256, and has `outcome=PASS`. It also checks full soak duration >= 7200, GUI cycles == 1000, cancellation revisions >= 10000 and matrix base cases == 336.

- [ ] **Step 5: Connect release readiness**

Set G2 Feature Complete to require the final Phase 12C verification matrix PASS. Do not modify G3 Windows/macOS/DAW requirements to PASS.

- [ ] **Step 6: Run the complete Linux acceptance command**

```bash
python3 scripts/run_phase12c_linux_validation.py \
  --build-dir build/phase12c-release \
  --output-dir out/phase12c \
  --full
```

- [ ] **Step 7: Commit**

```bash
git add scripts .github/workflows/phase12c-linux.yml docs/phase12c docs/RELEASE_READINESS* 
git commit -m "test: record Phase 12C Linux runtime acceptance"
```
