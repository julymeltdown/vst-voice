# Project SEAM Phase 11 구현 보고서

## 1. 단계 목표

Phase 11은 Phase 10의 headless CLAP render player 위에 다음 실제 편집·합성 경로를 추가한다.

```text
CLAP Host
→ Native Child GUI
→ Piano Roll / Phoneme / Unit / Seam / Pitch 편집
→ Revision 기반 비동기 Preview Render
→ bounded immutable PCM publication
→ CLAP Audio Process
```

또한 CLAP Note On/Off를 받아 공개 도메인 인간 음성 샘플을 피치 변환·루프하는 16-voice live sample instrument를 구현했다.

## 2. 구현 내용

### 2.1 CLAP GUI Extension

- `clap.gui` 구현
- X11 child window 실제 런타임 검증
- Win32 child window 소스 구현
- Cocoa `NSView` child view 소스 구현
- create / parent / resize / scale / show / hide / destroy
- Host timer-support를 통한 UI tick
- plugin state load 중 활성 GUI가 존재하면 restart 요청 후 거부

### 2.2 DAW 내부 편집기

Standalone에서 사용하던 first-party 도메인·Command·Editor Controller를 재사용했다.

- Piano Roll
- Note 선택과 drag
- Unicode lyric 편집 경로
- Phoneme Lane
- Unit Lane
- 개별 Seam 편집
- Pitch Automation Lane
- Character 01의 비침해형 compact dock

캐릭터 표시 상태는 합성 결과, PCM identity, state checksum에 관여하지 않는다.

### 2.3 비동기 Preview Render Service

- 편집 시 immutable Project snapshot 제출
- revision 기반 최신 요청 식별
- 대기 중 요청 교체와 stop-token 취소
- stale 결과 게시 거부
- 3-slot bounded publication
- Audio callback에서 lock·allocation·파일 I/O 없이 읽기

### 2.4 Live Note-event Singing

- CLAP note input port 1개
- CLAP dialect note-on / note-off
- sample offset 기준 이벤트 적용
- 최대 16 voice
- key 기반 pitch ratio
- attack / release envelope
- voice stealing
- 인간 음성 샘플 loop
- stereo output

### 2.5 State

`SEAMED11` bounded state에 Project JSON과 SHA-256을 저장한다.

- 최대 16 MiB
- partial stream read/write
- checksum 검증
- 손상된 state 거부
- 활성 plugin 또는 열린 GUI에서 state load 거부
- Host restart 요청
- 비활성 instance의 round trip 검증

### 2.6 Human Voice Demo Fixture

`assets/demo-human-voicebank-public-domain`에는 Sonic 저장소의 `samples/talking.wav`에서 파생한 작은 인간 음성 기술 fixture가 포함된다.

- 원본 WAV 보존
- 파생 vowel-like WAV 보존
- source / derived SHA-256
- source URL과 retrieval date
- 가공 과정
- upstream public-domain statement
- `official=false`
- `contractedSinger=false`

이 fixture는 기술 검증용이며 Official Voicebank 01이 아니다.

## 3. 패키징·플랫폼 소스

다음은 source/CI pipeline이 준비됐다.

- macOS `.clap` bundle metadata
- macOS codesign / notarytool / stapler
- macOS PKG 생성
- Windows package / Authenticode scripts
- Win32 child GUI
- Cocoa child GUI
- pinned clap-wrapper 기반 VST3/AUv2 build entry

Linux에서 실제 Apple notarization, Windows signing, VST3/AU target binary를 실행했다고 주장하지 않는다.

## 4. 실행 검증

| 검증 | 결과 |
|---|---:|
| 기존 Named Tests | 128 PASS / 0 FAIL |
| Phase 11 Release CTest | 3/3 PASS |
| Phase 11 ASan+UBSan CTest | 3/3 PASS |
| Phase 11 ThreadSanitizer core | PASS |
| Phase 11 ThreadSanitizer dynamic host | PASS |
| X11 child GUI 생성·표시 | PASS |
| CLAP Note Input | PASS |
| Live sample callback capture | PASS |
| State round trip | PASS |
| Active state-load restart policy | PASS |
| Release dynamic exports | `clap_entry` 1개 |
| Source / rights contract | PASS |
| License audit | PASS |
| Master-only branch policy | PASS |

Dynamic Host 결과:

```json
{
  "pluginId": "com.project-seam.editor",
  "guiCreated": true,
  "guiVisible": true,
  "screenshotWritten": true,
  "noteInputEnergy": 389.93,
  "capturedFrames": 24576,
  "audioWritten": true,
  "activeLoadRejected": true,
  "inactiveGuiLoadAccepted": true,
  "stateRoundTrip": true,
  "restartRequests": 1,
  "processRequests": 3,
  "result": "PASS"
}
```

## 5. 완료되지 않은 외부 Gate

다음은 완료로 표시하지 않는다.

- 공식 `clap-validator`: 현재 실행 환경에 binary가 없어 `NOT_RUN`
- 실제 REAPER / Bitwig / Cubase / Ableton / Studio One / FL Studio / Logic 인증
- Windows 실제 runtime 인증
- macOS 실제 runtime 인증
- VST3 binary와 validator
- AU binary와 `auval`
- Apple notarization
- Windows Authenticode
- 계약·디렉팅 녹음이 완료된 Official Voicebank 01

## 6. 제품 상태

현재 상태는 **Linux/X11에서 실제 동작하는 CLAP Embedded Editor Feature Alpha**다. DAW 내부 편집기, 비동기 preview render, note-event live sample instrument와 state 경계는 구현·검증됐다. 상용 배포 인증과 공식 보이스뱅크 제작은 별도 외부 Gate다.
