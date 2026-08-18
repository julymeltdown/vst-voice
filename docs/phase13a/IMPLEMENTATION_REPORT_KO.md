# Project SEAM Phase 13A 구현 보고서

## 목적

Phase 13A는 Project SEAM의 canonical CLAP 구현을 변경하지 않고 VST3와
macOS AUv2 배포 형식으로 투영하며, Validator·서명·notarization·Installer·
상용 DAW 검증을 **실제 증적 없이 PASS로 기록할 수 없게 하는 배포 Gate**를
구축한다.

## 구현된 범위

### 정확히 고정된 permissive 의존성

- CLAP 1.2.10, commit `195b42a004144fab0b3cf95e9c067187d15365b7`, MIT
- clap-wrapper 0.15.1, commit `35f524b771ec09f54c164720bb90f271273b37d3`, MIT
- VST3 SDK 3.8.1, commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`, MIT
- AudioUnitSDK 1.4.0, commit `bd98b31feff57a15989fcfab4cd86dc63382b1ac`, Apache-2.0
- NSIS 3.12, Zlib, Windows installer용 build-only tool

모든 SDK는 full commit, license, recursive submodule 상태를 확인하고
wrapper configure 단계의 네트워크 다운로드를 금지한다.

### Plug-in format pipeline

```text
Canonical ProjectSEAMEditor.clap
→ pinned clap-wrapper
→ Linux / Windows / macOS VST3
→ macOS AUv2
```

- Windows VST3는 서명 가능한 single-file 형식
- macOS VST3·AUv2는 canonical CLAP bundle을 내부에 포함
- 모든 note-expression을 canonical CLAP 구현으로 전달
- wrapper가 canonical CLAP을 찾도록 Validator 실행에 명시적 `CLAP_PATH` 전달
- VST3는 Steinberg `validator`, AUv2는 `auval` 원문 로그와 결과 JSON 보존

### 배포·서명·설치

- 결정적인 unsigned Linux developer ZIP
- 실제 HOME을 건드리지 않는 Linux sandbox install/uninstall smoke
- NSIS 3.12·zlib compressor 기반 Windows installer
- Windows CLAP·VST3 payload Authenticode 및 timestamp 검증
- Windows installer Authenticode 및 timestamp 검증
- Windows clean install·same-version reinstall·uninstall 결과 JSON
- macOS nested codesign·hardened runtime·timestamp
- Developer ID Installer로 서명된 PKG
- `notarytool` 제출·Accepted 확인·stapling·Gatekeeper 검증
- macOS clean install·same-version reinstall·uninstall 결과 JSON

인증서나 notarization credential이 없으면 release workflow는 fail-closed로
종료한다.

### 상용 DAW와 Release Gate

실제 DAW 결과는 OS/Host version, plug-in format, plug-in SHA-256, 실행자,
실행시각, 전체 필수 check, 원문 증적과 증적 SHA-256이 있어야만 `PASS`가
가능하다. 상태 단일 원본은 `mandatory-validation-matrix.json`이다.

## 로컬에서 실제 검증한 범위

```text
Warnings-as-errors Release Build     PASS
Full CTest                           39 / 39 PASS
Phase 13A Python Tests               33 / 33 PASS
Phase 13A Source Contract            PASS
Deterministic Linux Developer ZIP    PASS
Linux Sandbox Install/Uninstall      PASS
Workflow YAML Parsing                PASS
Master-only Policy                   PASS
License Audit                        PASS
```

현재 Linux CLAP SHA-256은 `local-verification.json`과 개발 패키지 manifest에
기록한다.

## 실제 대상 환경에서 반드시 검증할 범위

다음 항목은 **추후 선택 사항이 아니라 출시 차단 필수 검증**이다.

- Phase 12C 공식 `clap-validator` 및 정확한 7,200초 full soak
- Linux·Windows·macOS VST3 실제 build와 각 OS의 Steinberg Validator
- macOS AUv2 실제 build와 `auval -v aumu SEAM PSEM`
- Windows Authenticode·timestamp
- Apple Developer ID·notarization·stapling
- 깨끗한 Windows·macOS install·update·uninstall
- REAPER, Bitwig Studio, Cubase, Ableton Live, Studio One, FL Studio,
  Logic Pro, GarageBand 실제 Host 검증
- Official Voicebank 01의 계약·녹음·라벨링·수락

이 항목들은 실제 증적을 저장할 때까지 `NOT_RUN`이다. 소스·CI·binary의
존재는 `PASS`가 아니다.

## 현재 정확한 상태

```text
Engineering implementation    SOURCE_READY / CI_CONFIGURED
Local unsigned package        PASS
VST3 target build             NOT_RUN in this environment
Steinberg Validator           NOT_RUN in this environment
AUv2 / auval                   NOT_RUN; macOS required
Signing / notarization        NOT_RUN; credentials and target OS required
Commercial DAW matrix         NOT_RUN
Phase 13A release acceptance  BLOCKED
Product stage                 G1 Feature Alpha
```

다음 개발 단계는 Phase 13B의 Official Voicebank 01·Character/IP release
assets이지만, Phase 12C·13A mandatory validation은 병렬로 실제 환경에서
반드시 수행해야 한다.
