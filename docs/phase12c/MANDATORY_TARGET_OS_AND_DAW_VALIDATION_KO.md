# Phase 12C 필수 후속 Target OS·DAW 검증

> **이 문서의 테스트는 선택 사항이 아니다.** Windows, macOS 및 상용 DAW 검증이 실제 대상 운영체제와 실제 호스트에서 실행되고 증적이 저장되기 전에는 Project SEAM을 Beta, Release Candidate 또는 General Availability로 승격할 수 없다.

구현 상태(`NOT_STARTED`, `SOURCE_READY`, `CI_CONFIGURED`, `TARGET_BUILD_PASS`)와 실제 결과(`NOT_RUN`, `BLOCKED`, `FAIL`, `PASS`)를 분리한다. 소스가 존재하거나 CI가 구성됐다는 사실은 `PASS`가 아니다.

## 필수 대상

- Windows: Win32, TSF, WASAPI, 실제 CLAP 호스트
- macOS: Cocoa, NSTextInputClient, CoreAudio, `.clap` Bundle
- DAW: REAPER, Bitwig Studio, Cubase, Ableton Live, Studio One, FL Studio, Logic Pro, GarageBand

## 증적 필수 필드

OS/DAW 버전, Plugin SHA-256, 실행일, 담당자, scan 결과, GUI lifecycle, state restore, transport, tempo automation, offline export, channel/sample-rate/buffer matrix, unload/reload, 로그·스크린샷·오디오 파일 경로.

## Release Gate

- G2: Linux `clap-validator`, 336-case matrix, allocation probe, 2시간 full soak 모두 PASS
- G3: Windows Runtime PASS, macOS Runtime PASS, REAPER·Bitwig·Logic 실제 PASS
- G4: 선언된 전체 DAW PASS, signing/notarization, clean installer, VST3/AU validator PASS
- G5: Official Voicebank 01 및 최종 EULA 포함, mandatory unresolved count 0
