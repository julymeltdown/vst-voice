# Phase 12C 이후 반드시 수행해야 하는 Target OS 및 DAW 검증

> **이 문서의 검증은 선택 사항이 아니다.**  
> Windows, macOS 및 상용 DAW 테스트가 실제 대상 운영체제와 실제 호스트에서 수행되고 증적이 저장되기 전에는 Project SEAM을 Beta, Release Candidate 또는 General Availability로 승격할 수 없다.

## 상태 규칙

테스트 결과는 다음 네 값만 사용한다.

```text
NOT_RUN  아직 실제 대상에서 실행하지 않음
BLOCKED  인증서·장비·호스트 라이선스 등 외부 조건으로 실행 불가
FAIL     실제 실행했으며 하나 이상의 필수 항목 실패
PASS     실제 대상에서 전체 필수 항목 통과
```

소스 구현 상태는 별도 필드로 관리한다.

```text
NOT_STARTED
SOURCE_READY
CI_CONFIGURED
TARGET_BUILD_PASS
```

`SOURCE_READY`, `CI_CONFIGURED`, `TARGET_BUILD_PASS`는 런타임 테스트 `PASS`가 아니다.

## Windows 필수 검증

현재 결과: `NOT_RUN`

필수 범위:

- Windows 11 x64 실제 장비 또는 신뢰 가능한 전용 VM
- Win32 CLAP child window 생성·resize·focus·destroy
- Korean/Japanese TSF·IME composition
- WASAPI output·input
- CLAP scan·load·state restore
- Host play/stop/seek/loop/tempo automation
- 지원 sample rate와 buffer size matrix
- GUI 1,000회 lifecycle
- 2시간 soak
- Authenticode 서명
- Clean OS install·update·uninstall

## macOS 필수 검증

현재 결과: `NOT_RUN`

필수 범위:

- 지원 대상 macOS Apple Silicon 실제 장비
- Cocoa child `NSView` lifecycle
- Korean/Japanese `NSTextInputClient` composition
- CoreAudio output·input
- `.clap` bundle scan·state restore
- Host play/stop/seek/loop/tempo automation
- 지원 sample rate와 buffer size matrix
- GUI 1,000회 lifecycle
- 2시간 soak
- Developer ID signing
- Apple notarization·stapling
- Clean OS PKG install·update·uninstall

## 상용 DAW 필수 검증

| Host | OS | 현재 결과 | Beta 필수 | RC 필수 |
|---|---|---:|---:|---:|
| REAPER | Windows, macOS | NOT_RUN | 예 | 예 |
| Bitwig Studio | Windows, macOS | NOT_RUN | 예 | 예 |
| Logic Pro | macOS | NOT_RUN | 예 | 예 |
| Cubase | Windows, macOS | NOT_RUN | 아니오 | 예 |
| Ableton Live | Windows, macOS | NOT_RUN | 아니오 | 예 |
| Studio One | Windows, macOS | NOT_RUN | 아니오 | 예 |
| FL Studio | Windows, macOS | NOT_RUN | 아니오 | 예 |
| GarageBand | macOS | NOT_RUN | 아니오 | 예 |

각 Host에서 반드시 검증할 항목:

- Plugin scan
- GUI open·close·resize·focus
- Project save·reload와 plugin state restore
- Play·stop·seek·loop
- Tempo automation
- Note event와 expression
- Offline export
- 1·2·4·8채널 구성 중 Host 지원 범위
- Sample rate 변경
- Buffer size 변경
- Plugin unload·reload
- 장시간 재생 중 crash·hang·audio corruption 부재

## 증적 요구사항

각 실행은 다음을 기록한다.

```text
Host name and exact version
OS exact version and build
CPU architecture
Project SEAM commit
Plugin binary SHA-256
Voicebank ID, version, content hash
Test start/end timestamp
Operator
Individual test results
Screenshot or screen recording path
Crash/log path
Final status
```

상용 Host가 설치되지 않은 경우 결과는 `NOT_RUN`이다. 소스 코드 검사, first-party mock host 또는 CI 구성만으로 상용 Host를 `PASS`로 바꿀 수 없다.

## Release Gate

```text
G2 Feature Complete
- Linux Phase 12C validator·stress·soak PASS

G3 Beta
- Windows PASS
- macOS PASS
- REAPER PASS
- Bitwig Studio PASS
- Logic Pro PASS

G4 Release Candidate
- 지원 대상으로 선언한 전체 DAW PASS
- Signing·notarization PASS
- Clean OS installer PASS
- VST3/AU validator PASS

G5 General Availability
- Official Voicebank 01 acceptance PASS
- 이 문서와 JSON matrix의 unresolved count = 0
```
