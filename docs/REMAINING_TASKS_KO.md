# Project SEAM 잔여 작업 백로그

상태 값:

```text
DONE            구현·해당 자동 검증 완료
PARTIAL         수직 경로는 있으나 Acceptance 미완성
SOURCE_READY    대상 소스가 존재함
CI_CONFIGURED   실제 대상 CI가 구성됨
EXTERNAL_GATE   인증서·상용 Host·실연자 등 외부 자원이 필요
NOT_RUN         실제 실행 증적 없음
NOT_STARTED     착수 전
```

## Phase 12A·12B

- SEAM-P12-001 Production Preview: **DONE**
- SEAM-P12-002 Voicebank resolution/trust/relink: **DONE**
- SEAM-P12-003 Phoneme·Unit·Pitch direct editing: **DONE**
- SEAM-P12-004 Host tempo·loop·seek: **DONE**
- SEAM-P12-005 Multi-track·region·1~8 channel: **DONE**

## Phase 12C

### SEAM-P12-006 — Voicebank-driven live articulation
**상태: DONE — 구현 스냅샷**

Attack·Transition·Sustain·Release, legato, CLAP Note Expression, MIDI 1, 32 voices와 voice-stealing de-click이 구현됐다.

### SEAM-P12-007 — 공식 CLAP Validator
**상태: CI_CONFIGURED / NOT_RUN**

### SEAM-P12-008 — Windows/macOS 실제 Runtime
**상태: CI_CONFIGURED / NOT_RUN**

### SEAM-P12-009 — Full realtime/soak acceptance
**상태: PARTIAL**

336-case Matrix와 stress 도구는 있으나 정확한 7,200초 full soak는 반드시 실제 실행해야 한다.

## Phase 13A — 배포·인증 Pipeline

### SEAM-P13-001 — VST3 build·validator
**상태: SOURCE_READY / CI_CONFIGURED / NOT_RUN**

### SEAM-P13-002 — AUv2 build·`auval`
**상태: SOURCE_READY / CI_CONFIGURED / NOT_RUN**

### SEAM-P13-003 — Signing·Notarization·Installer
**상태: PIPELINE_READY / EXTERNAL_GATE / NOT_RUN**

### SEAM-P13-004 — 상용 Host Certification
**상태: RECORDER_READY / NOT_RUN**

실제 DAW와 대상 OS에서 실행해야 하며 체크인 baseline은 모두 NOT_RUN이다.

## Phase 13B — 제품 Content/IP

### SEAM-P13-005 — 계약 기반 Official Voicebank 01
**상태: EXTERNAL_GATE**

- 실연자 선정과 서명 계약
- 디렉팅 녹음과 retake
- 전체 Unit 라벨링과 pitch/loop QA
- 4개 Renderer listening QA
- 서명된 `.seambank`
- 사용자 상업 이용 EULA

### SEAM-P13-006 — Character 01 제품 확정
**상태: EXTERNAL_GATE**

- 정식 이름·상표·도메인
- 최종 face/turnaround
- 생산용 low-poly model·LOD·표정·animation
- IP 양도/소유권
- key art와 공식 voice/character 구분 문구

### SEAM-P13-007 — Content/IP Release Tooling
**상태: DONE**

증적 Hash, Voicebank/Character Dossier, deterministic 개발 패키지와 fail-closed G5 Gate가 구현됐다. 실제 계약·녹음·상표·IP 승인을 대체하지 않는다.

## Phase 14

- Production UI architecture 결정
- 접근성·i18n·UX polish
- onboarding·manual·support·demo project
- RC와 release process

## 필수 검증 문서

```text
docs/phase12c/MANDATORY_TARGET_OS_AND_DAW_VALIDATION_KO.md
docs/phase13a/MANDATORY_VALIDATION_KO.md
docs/phase13a/mandatory-validation-matrix.json
docs/phase13b/MANDATORY_OFFICIAL_VOICEBANK_AND_CHARACTER_VALIDATION_KO.md
docs/phase13b/mandatory-validation-matrix.json
```

이 항목들은 선택 사항이 아니며 실제 PASS 전에는 Beta·RC·GA로 승격할 수 없다.

## 즉시 수행할 제품 핵심 Milestone — Usable Standalone Alpha

코드 소유 Standalone milestone은 구현됐다. canonical Usable Alpha Gate를 통과하려면 이제 다음을 완료해야 한다.

1. 실제 발음 Coverage가 있고 재배포 가능한 Voicebank를 준비한다.
2. 그 bank로 Finder에서 실행한 Apple Silicon UI 여정을 완료한다.
3. reopen, autosave recovery, master/stem export 실제 청취 증적을 기록한다.
4. 외부 player로 export한 WAV를 검증한다.
5. 실제 곡으로 30분 안정성 세션을 완료한다.
6. `docs/product/USABLE_ALPHA_ACCEPTANCE.md`의 20개 행 모두에 hash-bound 증적을 첨부한다.
