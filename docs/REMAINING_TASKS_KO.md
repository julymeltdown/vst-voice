# Project SEAM 잔여 작업 백로그

이 문서는 “소스가 존재함”, “현재 환경에서 실행 검증됨”, “상용 출시 가능함”을 구분한다.

상태 값:

```text
DONE            구현·필수 자동 검증 완료
PARTIAL         수직 경로는 있으나 제품 범위가 미완성
SOURCE_READY    대상 플랫폼 소스/CI 계약만 있음
EXTERNAL_GATE   인증서, 상용 호스트, 실연자 등 외부 자원이 필요
NOT_STARTED     구현 착수 전
```

## P0 — 제품 기능 차단 항목

### SEAM-P12-001 — CLAP Preview를 생산 합성 파이프라인에 연결

**상태:** NOT_STARTED  
**현재 문제:** `AsyncPreviewRenderService::render()`는 임베디드 단일 모음 샘플을 음표마다 반복한다. 실제 Voicebank Unit, Phonemizer 결과, Timing Solver와 네 Renderer를 사용하지 않는다.

**구현 범위:**

```text
Plugin Project snapshot
→ Installed Voicebank resolution
→ Phonemizer
→ Unit Candidate / Selector
→ Timing Solver
→ Raw / PSOLA / Spectral / Stretch dispatcher
→ SeamComposer
→ Phrase Cache
→ bounded realtime publication
```

**완료 조건:** Standalone full render와 CLAP async render가 동일한 입력에서 같은 Unit plan과 허용 오차 내 PCM을 만든다.

### SEAM-P12-002 — 플러그인 Voicebank 선택·해결·누락 복구

**상태:** NOT_STARTED

- 설치된 `.seambank` 검색과 신뢰 상태 표시
- Voicebank ID/version/content hash를 plug-in state에 보존
- 누락 또는 버전 불일치 시 silent fallback 금지
- Relink, read-only open, substitute bank 선택
- Character binding과 synthesis dependency 분리 유지

### SEAM-P12-003 — Phoneme·Unit·Pitch 직접 편집 완성

**상태:** PARTIAL

현재:

- Lane 렌더링: 완료
- Seam amount 클릭 편집: 완료
- Phoneme boundary drag: 미구현
- Unit candidate/variant/renderer 선택: 미구현
- Pitch point CRUD와 interpolation 편집: 미구현
- Sample Microscope 진입: 미구현

**완료 조건:** 모든 변경이 Command/Undo/Redo, 저장, state round trip, dirty Phrase invalidation과 연결된다.

### SEAM-P12-004 — Host tempo·loop·seek 동기화 계약

**상태:** PARTIAL

현재 Preview PCM은 Project TempoMap으로 렌더되지만 host beat position은 host의 현재 tempo로 frame에 환산된다. Host tempo가 Project tempo와 다르거나 tempo automation이 있을 때 정확한 동기화가 보장되지 않는다.

결정해야 할 정책:

1. Host tempo를 Project TempoMap의 권위로 사용해 비동기 재렌더하거나,
2. Project timeline을 seconds 기반으로 고정하고 host beat sync를 제한한다.

지원해야 할 항목:

- Host tempo change
- Loop range
- Seek/jump
- Project start offset
- Offline render
- Host sample-rate change
- Time-signature event

### SEAM-P12-005 — Multi-track·Multi-region·Multichannel 플러그인 통합

**상태:** PARTIAL

현재 CLAP Embedded Editor는 첫 VocalRegion과 고정 stereo output에 집중한다. Phase 6의 bus/matrix/multichannel routing을 플러그인에 연결해야 한다.

- Track/Region 선택 UI
- Region resize/move
- Track mute/solo/gain/pan
- 1–8채널 audio-port config
- Bus routing state
- Host port rescan
- Stem 또는 bus output 정책

### SEAM-P12-006 — Live Note 입력을 실제 가창 합성으로 확장

**상태:** PARTIAL

현재는 단일 모음 loop sampler다. 정식 “Live Singing”으로 표기하려면 다음이 필요하다.

- 선택 Voicebank의 phoneme/attack/release unit 사용
- MIDI note-on/off 외 lyric/phoneme source 정책
- Legato와 note transition
- Pitch bend, expression, velocity mapping
- Voice stealing click 방지
- CLAP note expression
- MIDI 1 dialect 호환
- 최소 latency와 CPU budget

## P0 — 품질·검증 차단 항목

### SEAM-P12-007 — 공식 CLAP Validator 실행

**상태:** EXTERNAL_GATE / CI_CONFIGURED

현재 로컬 증적은 `NOT_RUN`이다. 공식 validator 전체 suite와 선택적 fuzz를 실행하고 결과를 저장해야 한다. 실패 항목은 release blocker다.

### SEAM-P12-008 — Windows/macOS 실제 런타임 인증

**상태:** SOURCE_READY

각 대상 OS에서 다음을 실제 실행한다.

- Native child window lifecycle
- IME 조합과 후보창 위치
- DPI/Retina resize
- WASAPI/CoreAudio physical device
- sleep/wake, device switch, sample-rate change
- plug-in unload/reload
- screenshot와 audio capture

### SEAM-P12-009 — 실시간·장시간 안정성

**상태:** PARTIAL

- 16/32/64/128/512/1024 frame buffer
- 44.1/48/88.2/96/192 kHz
- 2시간 재생·편집 soak
- UI open/close 반복 1,000회
- state load/save 반복
- renderer 취소 storm
- memory high-water mark
- audio-thread allocation detector
- TSan/ASan/UBSan target-platform runs

## P1 — 포맷·배포 차단 항목

### SEAM-P13-001 — VST3 실제 빌드·검증

**상태:** SOURCE_READY

- pinned clap-wrapper와 VST3 SDK audit
- Windows/macOS `.vst3` 생성
- Steinberg validator
- GUI, state, note input, offline render
- 실제 DAW scan

### SEAM-P13-002 — AUv2 실제 빌드·검증

**상태:** SOURCE_READY

- macOS AUv2 build
- `auval`
- Logic Pro/GarageBand scan
- state restore와 GUI lifecycle

### SEAM-P13-003 — Signing·Notarization·Installer

**상태:** EXTERNAL_GATE

- Windows Authenticode certificate
- macOS Developer ID Application/Installer
- notarization과 stapling
- Windows installer 또는 MSI
- macOS PKG
- clean OS install/update/uninstall
- rollback과 receipt 검증

### SEAM-P13-004 — 상용 Host Certification

**상태:** NOT_RUN

최소 Matrix:

- REAPER
- Bitwig Studio
- Cubase
- Ableton Live
- Studio One
- FL Studio
- Logic Pro / GarageBand

각 행에 정확한 OS, architecture, host version, plugin format, buffer/sample rate, GUI, state, transport, offline render 결과를 첨부한다.

## P1 — 콘텐츠·IP 차단 항목

### SEAM-P13-005 — Official Voicebank 01

**상태:** EXTERNAL_GATE

- 실제 실연자 선정
- 서명 계약·권리 검토
- Recording script와 range test
- 디렉팅 녹음·retake
- 전체 Unit 라벨링
- Pitch mark와 loop QA
- 4 Renderer listening QA
- signed `.seambank`
- 사용자 상업 이용 EULA

공개 도메인 Phase 11 fixture는 이 Gate를 통과할 수 없다.

### SEAM-P13-006 — Character 01 제품 확정

**상태:** PARTIAL

- 정식 이름
- 최종 face/turnaround
- 실제 생산용 low-poly 3D model
- 표정·LOD·animation
- IP ownership/양도 검토
- 상표·도메인 조사
- Voicebank와 performer를 구분하는 공식 문구
- key art와 사용 가이드

## P2 — 제품 완성도

### SEAM-P14-001 — UI 렌더링 아키텍처 결정

**상태:** DECISION_REQUIRED

현재 first-party software raster UI를 유지할지, 초기 계획의 iPlug2+Skia adapter를 감사 후 도입할지 결정한다. 두 경로를 동시에 장기 유지하지 않는다.

평가 기준:

- Host child-window 안정성
- CJK text
- 10,000 Note 성능
- GPU context 충돌
- dependency closure와 license
- 접근성
- 유지보수 비용

### SEAM-P14-002 — UX·접근성·국제화

- 단축키 설정
- Focus navigation
- screen-reader 접근 전략
- 고대비와 색각 보조
- UI scaling
- 한국어·일본어·영어 UI strings
- 복잡 문자 shaping의 제품 범위 결정
- Character animation off/reduced motion

### SEAM-P14-003 — 사용자 문서와 온보딩

- 설치 가이드
- 첫 곡 튜토리얼
- Voicebank 제작 가이드
- Seam/Renderer 설명
- Troubleshooting
- Crash diagnostic bundle
- Sample project와 demo song
- EULA/Privacy/Voicebank license

## 권장 실행 순서

```text
Phase 12A  Production plug-in render integration
Phase 12B  Technical lane editing + host timeline/routing
Phase 12C  Validator + target OS runtime hardening
Phase 13A  VST3/AU + installers + host matrix
Phase 13B  Official Voicebank + Character/IP release assets
Phase 14   UX polish, docs, RC, release
```
