# Phase 13A 추후 필수 검증 — 실행 전에는 완료가 아님

> **이 문서의 모든 항목은 반드시 추후 실제 대상 운영체제·Validator·DAW 환경에서 검증해야 한다.**
> 소스 코드, CI 정의, 빌드 스크립트, 생성된 바이너리 또는 체크리스트만으로는
> 테스트 `PASS`가 되지 않는다. 증적이 없는 항목은 `NOT_RUN`으로 유지하며,
> `NOT_RUN`·`BLOCKED`·`FAIL` 중 하나라도 남아 있으면 해당 Release Gate를 통과할 수 없다.

## 반드시 실제로 수행할 항목

1. Linux·Windows·macOS에서 VST3 target build와 Steinberg Validator 전체 실행
2. macOS에서 AUv2 target build와 `auval -v aumu SEAM PSEM` 전체 실행
3. Windows Authenticode 서명·timestamp·검증
4. macOS nested codesign, notarization, stapling, Gatekeeper 검증
5. 깨끗한 Windows·macOS에서 설치·동일 버전 재설치·업데이트·삭제
6. 실제 REAPER, Bitwig Studio, Cubase, Ableton Live, Studio One, FL Studio, Logic Pro, GarageBand 검증
7. 각 Host에서 scan, GUI lifecycle, state restore, transport, tempo automation, offline export, unload/reload, 1·2·4·8채널, sample-rate·buffer matrix 검증
8. Phase 12C의 공식 `clap-validator`와 정확한 7,200초 full soak
9. Official Voicebank 01의 실제 계약·녹음·라벨링·수락과 최종 EULA

## PASS 증적 필수 필드

```text
운영체제와 버전
DAW 또는 Validator 버전
Plugin Format
Plugin SHA-256
실행 시각
실행 담당자
모든 필수 Check의 PASS
원문 로그
화면 캡처
오디오 또는 Export 결과
각 증적 파일 SHA-256
```

## 승격 차단

- G3 Beta: Windows·macOS Runtime, REAPER, Bitwig Studio, Logic Pro 실제 PASS 필수
- G4 Release Candidate: 전체 Validator·DAW·서명·notarization·clean installer 실제 PASS 필수
- G5 General Availability: G4 전체와 Official Voicebank 01·EULA, unresolved mandatory 0 필수

상태의 단일 원본은 `mandatory-validation-matrix.json`이다. 이 파일에서 `NOT_RUN`인 항목은 선택적 백로그가 아니라 **반드시 실행해야 할 출시 차단 테스트**다.
