# Project SEAM Phase 12C Implementation Plan Index

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Phase 12C를 세 개의 독립적으로 검토 가능한 실행 계획으로 나누고, Linux에서 실제 검증을 완료하는 동시에 Windows·macOS·상용 DAW 검증을 필수 후속 Gate로 유지한다.

**Architecture:** 첫 번째 계획은 실시간 Voicebank Articulation 엔진과 CLAP/MIDI 이벤트 통합을 구현한다. 두 번째 계획은 공식 `clap-validator`, 336-case Linux matrix, 2시간 soak, GUI 1,000회, cancellation 10,000회와 sanitizer·realtime 검증을 실행한다. 세 번째 계획은 Windows·macOS 런타임 하네스, 상용 DAW 결과 스키마와 Release Gate를 구성하되 실제 대상 환경에서 실행되기 전까지 테스트 결과를 `NOT_RUN`으로 유지한다.

**Tech Stack:** C++20, CMake, CLAP 1.2.10 ABI, existing Project SEAM Voicebank/Rendering modules, Python 3 evidence tooling, GitHub Actions, ASan/UBSan/TSan.

**Spec:** `docs/superpowers/specs/2026-08-18-phase12c-runtime-validation-design.md`

## Global Constraints

- 모든 변경은 `master` 단일 브랜치에서만 수행한다.
- 런타임 의존성은 MIT, MIT-0, BSD-2-Clause, BSD-3-Clause, ISC, Apache-2.0, Zlib, BSL-1.0, CC0-1.0만 허용한다. OFL-1.1은 폰트에만 허용한다.
- Audio callback에서 파일 I/O, JSON/manifest parsing, 동적 메모리 할당, mutex, logging, Voicebank 탐색을 수행하지 않는다.
- Live resource decoded PCM 상한은 256 MiB, live voice 상한은 32, output channel 상한은 8, sample-rate 상한은 192 kHz다.
- 공식 `clap-validator`는 0.4.1 release를 exact commit으로 고정하고 원문 로그를 보존한다.
- Linux 필수 process matrix는 6 sample rates × 7 buffer sizes × 4 channel counts × 2 render modes = 336 cases다.
- 최종 soak는 정확히 7,200초, GUI lifecycle은 정확히 1,000회, cancellation storm은 최소 10,000 revision이다.
- Windows·macOS·상용 DAW 실제 실행 전에는 결과를 `PASS`로 기록하지 않는다.
- 공개 음성 fixture는 Official Voicebank 01 또는 contracted singer로 표시하지 않는다.

---

## 실행 순서

1. `2026-08-18-phase12c-live-voice-articulation.md`
2. `2026-08-18-phase12c-linux-runtime-validation.md`
3. `2026-08-18-phase12c-target-os-and-daw-gates.md`

첫 번째 계획의 public interfaces가 두 번째 계획의 matrix와 soak runner 입력이 된다. 세 번째 계획은 앞선 두 계획의 evidence schema와 binary identity를 소비한다.

## 커밋 순서

```text
feat: add immutable live voicebank resources
feat: implement live articulation planning and rendering
feat: integrate CLAP note expression and MIDI 1 live singing
test: add Phase 12C Linux runtime validation harnesses
test: record Phase 12C validator stress and soak evidence
ci: add mandatory Windows macOS and DAW validation gates
docs: complete Phase 12C evidence and release gate documentation
```

## 통합 Acceptance

- [ ] Live Voicebank attack/transition/sustain/release가 production Voicebank identity에서 해석된다.
- [ ] Legato와 transition fallback이 결정적으로 동작한다.
- [ ] CLAP note expression과 MIDI 1 pitch bend/CC mapping이 sample-offset 단위로 적용된다.
- [ ] 32 voice 제한과 de-click voice stealing이 callback 무할당 조건에서 동작한다.
- [ ] 공식 `clap-validator` 0.4.1 결과가 `PASS`다.
- [ ] Linux 336-case matrix가 전부 `PASS`다.
- [ ] 정확히 2시간 soak가 `PASS`다.
- [ ] GUI 1,000회와 cancellation 10,000 revision이 `PASS`다.
- [ ] ASan, UBSan, TSan, realtime allocation probe가 `PASS`다.
- [ ] Windows·macOS·상용 DAW 항목은 실행 증적이 없으면 `NOT_RUN`이며 G3 이상 승격을 차단한다.
