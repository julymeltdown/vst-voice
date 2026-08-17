# Project SEAM 잔여 작업 백로그

상태 값:

```text
DONE            구현·필수 자동 검증 완료
PARTIAL         수직 경로는 있으나 제품 범위가 미완성
SOURCE_READY    대상 플랫폼 소스/CI 계약만 있음
EXTERNAL_GATE   인증서, 상용 호스트, 실연자 등 외부 자원이 필요
NOT_STARTED     구현 착수 전
```

## Phase 12A 완료

### SEAM-P12-001 — CLAP Preview 생산 합성 파이프라인 통합

**상태: DONE**

CLAP Preview와 직접 Engine Render가 공통 `ProductionRegionRenderer`를
사용하며 Phonemizer, Unit Selector, Timing Solver, 네 Renderer,
SeamComposer와 Phrase Cache를 공유한다. 동일 입력의 Unit plan과 PCM parity가
테스트된다.

### SEAM-P12-002 — Voicebank 선택·해결·누락 복구 기반

**상태: DONE**

- 표준 설치 Root·환경 Root·Bundle/Sidecar·명시적 Relink Root 검색
- ID/version/content hash exact resolve
- signed install receipt와 trust 상태
- missing/version/hash/untrusted 명시 오류
- Project와 plug-in state에 exact reference 보존
- refresh, add-root, exact-select API
- silent fallback 금지

그래픽 Bank Browser와 file-picker polish는 Phase 12B/P14 UX 범위다.

## P0 — Phase 12B 제품 기능 차단

### SEAM-P12-003 — Phoneme·Unit·Pitch 직접 편집 완성

**상태: PARTIAL**

- Phoneme boundary drag
- Unit candidate/variant/renderer 선택
- Pitch point 추가·이동·삭제와 interpolation
- Sample Microscope 진입
- Command/Undo/Redo, state, dirty Phrase invalidation 연동

### SEAM-P12-004 — Host tempo·loop·seek 동기화

**상태: PARTIAL**

Host tempo change/automation, loop, seek, project offset, time signature,
offline render와 sample-rate 변경의 권위 정책을 확정하고 구현한다.

### SEAM-P12-005 — Multi-track·Multi-region·Multichannel plug-in 통합

**상태: PARTIAL**

Track/Region UI, mute/solo/gain/pan, Phase 6 bus/matrix routing, 1–8채널
port config, host rescan과 stem/bus output 정책이 남아 있다.

### SEAM-P12-006 — Live Note 입력의 Voicebank 기반 가창 확장

**상태: PARTIAL**

현재 단일 모음 Live Sampler를 선택 Bank의 attack/release/transition,
legato, pitch bend, note expression, MIDI dialect와 de-click voice stealing으로
확장한다.

## P0 — Phase 12C 품질·검증 차단

### SEAM-P12-007 — 공식 CLAP Validator
**상태: EXTERNAL_GATE / CI_CONFIGURED**

### SEAM-P12-008 — Windows/macOS 실제 런타임 인증
**상태: SOURCE_READY**

### SEAM-P12-009 — 실시간·장시간 안정성
**상태: PARTIAL**

- Buffer 16~1024, 44.1~192 kHz
- 2시간 재생·편집 soak
- GUI 1,000회 open/close
- state 반복, cancellation storm
- allocation/high-water/ASan/UBSan/TSan 대상 플랫폼 증적

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
Phase 12B  Technical lane editing + host timeline/routing
Phase 12C  Validator + target OS runtime hardening
Phase 13A  VST3/AU + installers + host matrix
Phase 13B  Official Voicebank + Character/IP release assets
Phase 14   UX polish, documentation, RC, release
```
