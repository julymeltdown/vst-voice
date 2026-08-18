# Phase 13B 이후 반드시 수행해야 하는 검증

이 문서의 항목은 권고가 아니라 **출시 차단 조건**이다. 코드, 스크립트,
CI 또는 템플릿이 존재하는 것만으로는 검증 `PASS`가 아니다. 실제 대상,
실제 담당자, 실제 증적과 SHA-256이 저장되기 전에는 해당 항목을
`PASS`, `ACCEPTED`, Beta, Release Candidate 또는 General Availability로
표시할 수 없다.

## 상태 표현

```text
구현 상태
NOT_STARTED | SOURCE_READY | CI_CONFIGURED | TARGET_BUILD_PASS

실제 검증 결과
NOT_RUN | BLOCKED | FAIL | PASS
```

`TARGET_BUILD_PASS`와 실제 검증 `PASS`는 별개다.

## Phase 12C 필수 검증

- 공식 `clap-validator` 전체 검증
- 44.1/48/88.2/96/176.4/192kHz
- 16/32/64/128/256/512/1024 frame buffer
- 1/2/4/8채널
- Realtime 및 Offline render
- 정확한 7,200초 wall-clock full soak
- GUI 1,000회 open/close
- 10,000 revision cancellation storm
- realtime allocation·mutex·I/O 검출
- Windows 실제 CLAP runtime
- macOS 실제 CLAP runtime

## Phase 13A 필수 검증

- Linux·Windows·macOS 실제 VST3 build
- 각 운영체제의 Steinberg VST3 Validator
- macOS 실제 AUv2 build 및 `auval`
- Windows Authenticode 서명·timestamp
- Apple Developer ID 서명·notarization·stapling
- 깨끗한 Windows/macOS에서 설치·업데이트·재설치·삭제
- REAPER, Bitwig Studio, Cubase, Ableton Live, Studio One, FL Studio,
  Logic Pro, GarageBand 실제 Host 검증

## Official Voicebank 01 필수 검증

- 실제 실연자 선정·신원 확인
- 서명된 계약과 권리 검토
- 디렉팅된 실제 녹음 세션 로그
- 마이크·프리앰프·거리·gain calibration
- 전체 목표 음소·피치 레이어·발성 inventory
- retake 종료와 수락
- acoustic marker·pitch mark·loop QA
- Raw·Classic PSOLA·SpectralClassic·Stretch 청취 QA
- 서명된 `.seambank`
- 설치 receipt와 rollback 검증
- 사용자 상업 결과물 EULA
- 제품 책임자·법무 승인

공개 도메인·CC0 인간 음성은 기술 fixture로 사용할 수 있지만 위 계약과
녹음 세션을 대체하지 못한다.

## Character 01 필수 검증

- 최종 공개 이름
- 실제 상표·도메인·소셜 핸들 clearance
- 디자이너·3D 아티스트 IP 양도 또는 상업 이용 계약
- 원본 provenance와 파생 자산 hash
- 정면·측면·후면 turnaround
- production low-poly model·UV·LOD
- 표정·상태 portrait·animation
- 최종 key art
- merchandise·2차 창작 사용 정책
- Voice provider와 Character가 동일 인물이라는 오해 방지 문구
- 제품 책임자·법무 승인

현재 생성된 `production-development` 자산은 개발 증적이며 생산용
turnaround나 최종 3D 모델 승인 증적이 아니다.

## 증적 최소 필드

실제 `PASS`에는 최소한 다음이 필요하다.

```text
대상 ID
정확한 OS·Host·Validator 버전
Plugin/Package SHA-256
실행 시각
실행 담당자 또는 검수자
실행된 검사별 PASS 결과
원문 로그·스크린샷·오디오·문서의 상대 경로
각 증적 파일 SHA-256
```

누락, 경로 이탈, symlink, 빈 파일, hash mismatch가 있으면 `PASS`는
무효이며 Gate는 실패해야 한다.

## 승격 차단

```text
G2 Feature Complete
→ Phase 12C Linux 필수 검증 PASS

G3 Beta
→ Windows/macOS Runtime PASS
→ REAPER·Bitwig·Logic Pro PASS

G4 Release Candidate
→ 선언된 전체 DAW·VST3·AU·서명·Notarization·Installer PASS

G5 General Availability
→ Official Voicebank 01 ACCEPTED
→ Character 01 ACCEPTED
→ 최종 EULA·Voicebank License PASS
→ Mandatory unresolved count = 0
```
