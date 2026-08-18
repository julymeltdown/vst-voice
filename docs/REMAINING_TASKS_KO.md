# Project SEAM 잔여 작업 백로그

상태 값:

```text
DONE            구현·필수 자동 검증 완료
PARTIAL         수직 경로는 있으나 제품 범위가 미완성
SOURCE_READY    대상 플랫폼 소스/CI 계약만 있음
EXTERNAL_GATE   인증서, 상용 호스트, 실연자 등 외부 자원이 필요
NOT_STARTED     구현 착수 전
```

## Phase 12A·12B 완료

### SEAM-P12-001 — CLAP Preview 생산 합성 파이프라인 통합
**상태: DONE**

### SEAM-P12-002 — Voicebank 선택·해결·누락 복구 기반
**상태: DONE**

### SEAM-P12-003 — Phoneme·Unit·Pitch 직접 편집
**상태: DONE**

- Phoneme start/end boundary override
- Unit candidate/variant와 Renderer 선택·순환
- Pitch point 추가·이동·삭제·interpolation
- Sample Microscope 진입
- Command/Undo/Redo와 dirty render revision

### SEAM-P12-004 — Host tempo·loop·seek 동기화
**상태: DONE**

- seconds timeline 우선
- beats+tempo fallback
- loop·seek
- project host start offset
- time-signature metadata
- realtime/offline quality
- sample-rate activation contract

### SEAM-P12-005 — Multi-track·Multi-region·Multichannel plug-in 통합
**상태: DONE**

- 모든 audible vocal track와 region
- track mute/solo/gain/pan
- Phase 6 bus/matrix routing
- 1~8채널 CLAP output config
- audio-port/config rescan
- schema 5 round trip

### SEAM-P12-006 — Live Note 입력의 Voicebank 기반 가창 확장
**상태: PARTIAL**

현재 인간 모음 기반 16-voice sampler는 동작한다. 다음이 남아 있다.

- 선택 Voicebank attack/release/transition
- legato와 phoneme transition
- pitch bend와 CLAP note expression
- MIDI 1 dialect
- de-click voice stealing

## P0 — Phase 12C 품질·검증 차단

### SEAM-P12-007 — 공식 CLAP Validator
**상태: EXTERNAL_GATE / CI_CONFIGURED**

### SEAM-P12-008 — Windows/macOS 실제 런타임 인증
**상태: SOURCE_READY**

### SEAM-P12-009 — 실시간·장시간 안정성
**상태: PARTIAL**

- Buffer 16~1024
- 44.1~192 kHz
- 2시간 재생·편집 soak
- GUI 1,000회 open/close
- state 반복·render cancellation storm
- callback allocation/high-water/ASan/UBSan/TSan 대상 플랫폼 증적

## P1 — Phase 13 배포·콘텐츠 차단

### SEAM-P13-001 — VST3 실제 빌드·Validator·Host Scan
**상태: SOURCE_READY**

### SEAM-P13-002 — AUv2 실제 빌드·`auval`·Logic/GarageBand
**상태: SOURCE_READY**

### SEAM-P13-003 — Signing·Notarization·Installer
**상태: EXTERNAL_GATE**

### SEAM-P13-004 — 상용 Host Certification Matrix
**상태: NOT_RUN**

### SEAM-P13-005 — 계약 기반 Official Voicebank 01
**상태: EXTERNAL_GATE**

### SEAM-P13-006 — Character 01 제품 확정
**상태: PARTIAL**

## P2 — Phase 14 제품 완성도

### SEAM-P14-001 — Production UI 아키텍처 결정
**상태: DECISION_REQUIRED**

### SEAM-P14-002 — 접근성·국제화·UX polish
**상태: NOT_STARTED**

### SEAM-P14-003 — 사용자 문서·온보딩·지원 자료
**상태: PARTIAL**

## 권장 실행 순서

```text
Phase 12C  Validator + target OS runtime + realtime/soak hardening
Phase 13A  VST3/AU + installers + host matrix
Phase 13B  Official Voicebank + Character/IP release assets
Phase 14   UX polish, documentation, RC, release
```

## Phase 12C 승인 설계 및 강제 후속 검증

승인된 설계:

```text
docs/superpowers/specs/2026-08-18-phase12c-runtime-validation-design.md
```

Windows, macOS 및 상용 DAW 실제 테스트는 다음 문서와 JSON Matrix에서 강제한다.

```text
docs/phase12c/MANDATORY_TARGET_OS_AND_DAW_VALIDATION_KO.md
docs/phase12c/MANDATORY_TARGET_OS_AND_DAW_VALIDATION.md
docs/phase12c/mandatory-validation-matrix.json
```

이 항목들은 Phase 12C 소스 구현과 별개이며, 실제 대상에서 `PASS`가 되기 전에는 Beta·RC·GA Gate를 통과할 수 없다.
