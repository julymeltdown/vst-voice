# Phase 12C Target OS and Commercial DAW Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Windows·macOS와 상용 DAW 검증을 반복 가능한 runtime harness와 release-gate 데이터로 연결하고, 실제 대상 실행 전에는 결과가 `NOT_RUN`에서 `PASS`로 바뀌지 않게 한다.

**Architecture:** 구현 상태와 테스트 결과를 별도 enum으로 저장한다. Windows와 macOS CI는 target build와 first-party runtime harness를 실행해 platform evidence를 생성한다. 상용 DAW 결과는 실제 운영자가 지정된 schema와 evidence path로 기록하며, source-level test나 CI configuration만으로 PASS를 만들 수 없다.

**Tech Stack:** Python 3 schema/verification tooling, C++ target-runtime harnesses, GitHub Actions Windows/macOS runners, existing Win32/TSF/WASAPI and AppKit/NSTextInputClient/CoreAudio adapters.

**Spec:** `docs/superpowers/specs/2026-08-18-phase12c-runtime-validation-design.md`

## Global Constraints

- Implementation state: `NOT_STARTED`, `SOURCE_READY`, `CI_CONFIGURED`, `TARGET_BUILD_PASS`.
- Test result: `NOT_RUN`, `BLOCKED`, `FAIL`, `PASS`.
- `SOURCE_READY`, `CI_CONFIGURED`, `TARGET_BUILD_PASS`는 runtime PASS가 아니다.
- G3는 Windows runtime PASS, macOS runtime PASS, REAPER PASS, Bitwig PASS, Logic Pro PASS를 요구한다.
- G4는 선언된 전체 DAW, signing/notarization, clean-OS install/update/uninstall, VST3/AU validator PASS를 요구한다.
- Linux에서 Windows/macOS 또는 상용 DAW 결과를 PASS로 작성하는 경로를 제공하지 않는다.

---

### Task 1: Define and validate the mandatory matrix schema

**Files:**
- Modify: `docs/phase12c/mandatory-validation-matrix.json`
- Create: `scripts/validate_mandatory_validation_matrix.py`
- Create: `tests/test_phase12c_mandatory_matrix.py`
- Modify: `docs/phase12c/MANDATORY_TARGET_OS_AND_DAW_VALIDATION.md`
- Modify: `docs/phase12c/MANDATORY_TARGET_OS_AND_DAW_VALIDATION_KO.md`

**Interfaces:**

Each matrix entry must contain:

```json
{
  "id": "windows-clap-runtime",
  "category": "target-os",
  "implementationState": "CI_CONFIGURED",
  "testResult": "NOT_RUN",
  "requiredForGate": "G3",
  "osVersion": "",
  "hostName": "first-party-runtime-harness",
  "hostVersion": "",
  "pluginSha256": "",
  "operator": "",
  "executedAtUtc": "",
  "evidence": [],
  "diagnostic": "Target runtime has not been executed"
}
```

- [ ] **Step 1: Write schema validation tests**

Reject:

```text
unknown state values
PASS without non-empty pluginSha256
PASS without exact OS/host version
PASS without operator/date/evidence
SOURCE_READY coupled automatically to PASS
duplicate IDs
missing mandatory REAPER/Bitwig/Logic rows
```

- [ ] **Step 2: Implement the validator**

The script exits non-zero for invalid schema and prints the exact entry ID and field. It must not rewrite outcomes.

- [ ] **Step 3: Update both human-readable mandatory documents**

Put this statement at the start:

> Windows, macOS 및 상용 DAW 검증은 선택 사항이 아니다. 실제 대상 환경에서 실행되고 증적이 저장되기 전에는 Beta, Release Candidate 또는 General Availability로 승격할 수 없다.

- [ ] **Step 4: Commit**

```bash
git add docs/phase12c scripts/validate_mandatory_validation_matrix.py tests/test_phase12c_mandatory_matrix.py
git commit -m "test: formalize mandatory target runtime validation states"
```

---

### Task 2: Implement the Windows target runtime harness and CI evidence

**Files:**
- Create: `apps/seam-phase12c-windows-runtime/main.cpp`
- Create: `scripts/record_windows_runtime_evidence.ps1`
- Create: `.github/workflows/phase12c-windows-runtime.yml`
- Modify: `CMakeLists.txt`
- Modify: `docs/phase12c/mandatory-validation-matrix.json` implementation state only

**Interfaces:**
- Consumes existing `Win32EmbeddedView`, TSF/native lyric overlay, WASAPI input/output and `ProjectSEAMEditor.clap`.
- Produces:

```text
windows-runtime-summary.json
windows-ime-korean.png
windows-ime-japanese.png
windows-audio-matrix.json
windows-gui-lifecycle.json
```

- [ ] **Step 1: Implement CLAP scan/load and child GUI lifecycle**

Load the actual Windows `.clap` DLL through `LoadLibraryW`, resolve `clap_entry`, instantiate the plugin, create a parent HWND and execute GUI create/parent/show/resize/hide/destroy.

- [ ] **Step 2: Implement Korean and Japanese TSF/IME checks**

Use the native text overlay to commit `한글 가사` and `かな歌詞`, save plugin state, reopen the plugin, and assert exact Unicode round-trip. Capture screenshots after composition commit.

- [ ] **Step 3: Exercise WASAPI**

Use a real or CI virtual endpoint. Record device ID, supported sample rates and buffer periods. Unsupported device combinations are recorded as `BLOCKED`; supported combinations must process finite audio without underrun.

- [ ] **Step 4: Emit result without self-promoting to PASS**

The harness emits a candidate JSON. `record_windows_runtime_evidence.ps1` supplies operator, OS version, runner identity and evidence paths, then updates only `windows-clap-runtime`. A source checkout on Linux cannot run this script successfully.

- [ ] **Step 5: Configure CI**

The workflow builds warnings-as-errors, runs unit tests, executes the runtime harness, uploads all evidence, and calls the matrix validator. Set implementation state to `CI_CONFIGURED`; set `testResult=PASS` only in the generated CI artifact, not in the checked-in baseline matrix.

- [ ] **Step 6: Commit**

```bash
git add apps/seam-phase12c-windows-runtime scripts/record_windows_runtime_evidence.ps1 .github/workflows/phase12c-windows-runtime.yml CMakeLists.txt docs/phase12c/mandatory-validation-matrix.json
git commit -m "ci: add mandatory Windows CLAP runtime harness"
```

---

### Task 3: Implement the macOS target runtime harness and CI evidence

**Files:**
- Create: `apps/seam-phase12c-macos-runtime/main.mm`
- Create: `scripts/record_macos_runtime_evidence.sh`
- Create: `.github/workflows/phase12c-macos-runtime.yml`
- Modify: `CMakeLists.txt`
- Modify: `docs/phase12c/mandatory-validation-matrix.json` implementation state only

**Interfaces:**
- Consumes existing Cocoa child view, `NSTextInputClient`, CoreAudio adapters and macOS `.clap` bundle.
- Produces:

```text
macos-runtime-summary.json
macos-ime-korean.png
macos-ime-japanese.png
macos-coreaudio-matrix.json
macos-gui-lifecycle.json
macos-clap-scan.json
```

- [ ] **Step 1: Load the real `.clap` bundle**

Use `NSBundle` to load the built Mach-O bundle and resolve `clap_entry`. Record bundle ID, code-sign state, plugin SHA-256 and architecture.

- [ ] **Step 2: Verify Cocoa lifecycle and text composition**

Create an NSWindow/parent NSView, attach plugin child NSView, resize, hide/show and destroy. Commit Korean and Japanese text through the `NSTextInputClient` path and assert state round-trip.

- [ ] **Step 3: Exercise CoreAudio**

Record default device UID, supported nominal sample rates and callback frame sizes. Process input/output where available and record unsupported hardware combinations as BLOCKED.

- [ ] **Step 4: Configure macOS CI**

Build the `.clap` bundle, run the runtime harness, upload evidence, and validate the candidate result. Signing/notarization remain separate G4 entries; an unsigned CI plugin may pass G3 runtime but not G4 distribution gates.

- [ ] **Step 5: Commit**

```bash
git add apps/seam-phase12c-macos-runtime scripts/record_macos_runtime_evidence.sh .github/workflows/phase12c-macos-runtime.yml CMakeLists.txt docs/phase12c/mandatory-validation-matrix.json
git commit -m "ci: add mandatory macOS CLAP runtime harness"
```

---

### Task 4: Add the commercial DAW certification recorder

**Files:**
- Create: `tools/seam-host-certification-recorder/main.py`
- Create: `docs/phase12c/host-certification-schema.json`
- Create: `docs/phase12c/HOST_CERTIFICATION_PROCEDURE_KO.md`
- Create: `docs/phase12c/HOST_CERTIFICATION_PROCEDURE.md`
- Create: `tests/test_host_certification_recorder.py`
- Modify: `docs/phase12c/mandatory-validation-matrix.json`

**Interfaces:**

Command:

```bash
python3 tools/seam-host-certification-recorder/main.py record \
  --matrix docs/phase12c/mandatory-validation-matrix.json \
  --host reaper \
  --host-version "$HOST_VERSION" \
  --os-version "$OS_VERSION" \
  --plugin ./ProjectSEAMEditor.clap \
  --operator "legal-name-or-team-id" \
  --evidence evidence/reaper/session.json \
  --out out/reaper-result.json
```

- [ ] **Step 1: Define mandatory per-host cases**

```text
scan
GUI open/close/resize
state restore
play/stop/seek/loop
tempo automation
offline export
1/2/4/8-channel config
sample-rate change
buffer change
plugin unload/reload
crash/hang/audio corruption check
```

- [ ] **Step 2: Implement append-only result recording**

The tool writes a candidate result file and never edits the checked-in matrix automatically. A maintainer reviews evidence and merges the result through a separate explicit `apply` command.

- [ ] **Step 3: Prevent false PASS**

The `apply` command refuses PASS unless every mandatory case is PASS, evidence files exist, plugin SHA-256 matches, operator/date/version fields are non-empty, and the host ID is one of the declared matrix entries.

- [ ] **Step 4: Add mandatory rows**

At minimum:

```text
REAPER Windows
REAPER macOS
Bitwig Windows
Bitwig macOS
Cubase Windows
Cubase macOS
Ableton Live Windows
Ableton Live macOS
Studio One Windows
Studio One macOS
FL Studio Windows
FL Studio macOS
Logic Pro macOS
GarageBand macOS
```

All checked-in results remain `NOT_RUN` until actual evidence is applied.

- [ ] **Step 5: Commit**

```bash
git add tools/seam-host-certification-recorder docs/phase12c tests/test_host_certification_recorder.py
git commit -m "test: add commercial DAW certification recorder"
```

---

### Task 5: Connect maturity gates to mandatory runtime evidence

**Files:**
- Create: `scripts/evaluate_release_gate.py`
- Create: `tests/test_phase12c_release_gates.py`
- Modify: `docs/RELEASE_READINESS.md`
- Modify: `docs/RELEASE_READINESS_KO.md`
- Modify: `docs/phase12c/ACCEPTANCE.md`
- Modify: `docs/phase12c/mandatory-validation-matrix.json`

**Interfaces:**

```bash
python3 scripts/evaluate_release_gate.py \
  --gate G3 \
  --phase12c out/phase12c/phase12c-verification-matrix.json \
  --mandatory docs/phase12c/mandatory-validation-matrix.json
```

- [ ] **Step 1: Write gate truth-table tests**

Test exact requirements:

```text
G2: Phase 12C Linux acceptance PASS
G3: G2 + Windows runtime PASS + macOS runtime PASS + REAPER + Bitwig + Logic PASS
G4: G3 + every declared supported DAW PASS + signing/notarization + clean installer + VST3/AU validators
G5: G4 + Official Voicebank 01 + final licences + unresolved mandatory count 0
```

- [ ] **Step 2: Implement fail-closed evaluation**

Unknown, missing, NOT_RUN, BLOCKED or FAIL entries all block the gate. Output includes every blocking ID and reason.

- [ ] **Step 3: Add CI checks**

Normal PR CI validates schemas and asserts current maturity does not exceed available evidence. Release workflows request the target gate explicitly and fail if evidence is insufficient.

- [ ] **Step 4: Commit**

```bash
git add scripts/evaluate_release_gate.py tests/test_phase12c_release_gates.py docs/RELEASE_READINESS* docs/phase12c
git commit -m "ci: enforce mandatory target runtime release gates"
```

---

### Task 6: Final Phase 12C documentation and package verification

**Files:**
- Modify: `docs/phase12c/IMPLEMENTATION_REPORT.md`
- Modify: `docs/phase12c/EVIDENCE.md`
- Modify: `docs/phase12c/ACCEPTANCE.md`
- Create: `docs/phase12c/IMPLEMENTATION_REPORT_KO.md`
- Create: `scripts/package_phase12c.py`

**Interfaces:**
- Consumes all Linux evidence and mandatory target matrix.
- Produces a `.git`-included source ZIP, SHA-256, package verification log and copied evidence artifacts.

- [ ] **Step 1: Document exact completion boundary**

The report must say:

```text
Linux Phase 12C engineering validation: PASS or exact failure
Windows runtime: NOT_RUN unless actual target artifact exists
macOS runtime: NOT_RUN unless actual target artifact exists
Commercial DAW rows: NOT_RUN unless actual host evidence exists
Product maturity: G2 only when Linux acceptance passes; never G3 from source readiness
```

- [ ] **Step 2: Run package verification from a fresh extraction**

```text
.git exists
branch is master only
working tree clean
warnings-as-errors build
all Phase 12C tests
Linux evidence verifier
mandatory matrix schema validator
release gate evaluator
git fsck --full
archive SHA-256
```

- [ ] **Step 3: Commit**

```bash
git add docs/phase12c scripts/package_phase12c.py
git commit -m "docs: complete Phase 12C validation and release gates"
```
