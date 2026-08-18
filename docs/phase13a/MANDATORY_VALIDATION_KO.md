# Phase 13A 필수 검증 — VST3·AU·서명·설치·상용 DAW

> **이 문서의 검증은 필수이며 선택 사항이 아니다.** 실제 대상 운영체제와 실제 DAW 또는 공식 Validator에서 테스트하고 원문 증적을 저장하기 전에는 `PASS`로 기록할 수 없다.

소스가 존재하거나 CI가 구성된 상태는 구현 진척일 뿐이다.

```text
SOURCE_READY      ≠ BUILD PASS
CI_CONFIGURED     ≠ TARGET RUNTIME PASS
BINARY CREATED    ≠ VALIDATOR PASS
SIGNED            ≠ NOTARIZED
NOTARIZED         ≠ CLEAN INSTALL PASS
CHECKLIST EXISTS  ≠ DAW CERTIFICATION PASS
```

실제 검증 결과는 `NOT_RUN`, `BLOCKED`, `FAIL`, `PASS` 중 하나만 사용한다. `PASS`에는 OS 버전, Host 또는 Validator 버전, Plugin SHA-256, 실행 시각, 담당자, 원문 로그와 화면·오디오 증적 경로가 모두 있어야 한다.

## 반드시 검증할 포맷

- Linux·Windows·macOS에서 VST3 실제 target build
- 각 지원 OS에서 Steinberg VST3 Validator 전체 실행
- macOS에서 AUv2 실제 target build
- macOS에서 `auval -v aumu SEAM PSEM` 전체 실행
- CLAP canonical state와 VST3·AU state round trip 비교
- 1·2·4·8채널, 44.1·48·96·192kHz, 16~1024 frame matrix
- GUI 생성·리사이즈·종료, 프로젝트 저장·재오픈, offline export

## 반드시 검증할 서명과 설치

- Windows Authenticode 서명과 timestamp 검증
- macOS nested codesign, notarization, stapling, Gatekeeper 검증
- 깨끗한 Windows·macOS에서 설치·업데이트·삭제
- 사용자·시스템 설치 경로, rollback, 잔여 파일과 이전 버전 migration
- 설치 후 실제 Host scan, 프로젝트 저장·복원, uninstall 후 재검색

## 반드시 검증할 상용 DAW

- REAPER
- Bitwig Studio
- Cubase
- Ableton Live
- Studio One
- FL Studio
- Logic Pro
- GarageBand

모든 Host에서 Plugin scan, GUI lifecycle, state restore, play·stop·seek·loop, tempo automation, offline export, channel/sample-rate/buffer matrix와 unload/reload를 실제로 수행해야 한다.

## 승격 차단

- **Beta(G3):** Windows Runtime, macOS Runtime, REAPER, Bitwig Studio, Logic Pro가 실제 `PASS`여야 한다.
- **Release Candidate(G4):** 선언된 전체 VST3·AU Validator, 전체 DAW, 서명·notarization, clean installer가 실제 `PASS`여야 한다.
- **General Availability(G5):** G4 전체와 Official Voicebank 01 계약·녹음·수락, 최종 EULA가 완료되고 mandatory unresolved count가 0이어야 한다.

검증 상태의 단일 원본은 [`mandatory-validation-matrix.json`](mandatory-validation-matrix.json)이다. 현재 값이 `NOT_RUN`인 항목은 추후 반드시 실제 환경에서 수행해야 한다.
