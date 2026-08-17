# Project SEAM Phase 10 구현 보고서

## 1. 단계 목표

Phase 10에서는 Project SEAM의 첫 실제 플러그인 포맷 경계를 구현했다.
결과물은 `ProjectSEAM.clap`이라는 **로드 가능한 CLAP 1.2.10 모듈**이다.

다만 이 단계에서 전체 피아노롤 편집기와 샘플 연결 합성기를 DAW 내부에
억지로 넣지 않았다. Phase 10의 플러그인은 Standalone 에디터에서 미리
렌더링한 1~8채널 결과를 상태로 보유하고, DAW Transport에 맞춰 재생하는
**headless render player**다.

```text
Standalone Editor
→ Sample-concatenative Render
→ SEAMCLP1 State
→ ProjectSEAM.clap
→ Host Transport-synchronized Playback
```

이 범위를 먼저 분리한 이유는 다음 계약을 작은 표면에서 검증하기 위해서다.

- 플러그인 수명주기
- 동적 모듈 로딩
- Factory와 Descriptor
- Audio Port
- Host Transport
- Sample-accurate Parameter Automation
- Persistent State
- Audio-thread 실시간 안전성
- Multichannel PCM

## 2. 구현 결과

### 2.1 실제 CLAP 모듈

다음 모듈을 생성한다.

```text
ProjectSEAM.clap
```

Plugin ID:

```text
com.project-seam.render-player
```

주요 특성:

- Audio Input 없음
- Main Audio Output 1개
- State에 따라 1~8채널 출력
- Latency 0 frame
- Tail 0 frame
- Realtime/Offline Render Mode 수용
- Host seconds timeline 우선
- Beat + Tempo fallback
- Transport가 없으면 free-run
- Transport가 멈췄으면 결정적인 silence 출력

### 2.2 Master Gain Parameter

하나의 안정된 Parameter를 구현했다.

```text
Name        Master Gain
Range       -60 dB ~ +6 dB
Default      0 dB
Flags        Automatable / Requires Process
```

Parameter Value Event는 `process()` 블록의 sample offset에서 반영된다.
4채널 동적 Host Smoke에서 frame 128에 `-6 dB` event를 넣어 다음 비율을
확인했다.

```text
Linear gain ratio = 0.501187
```

### 2.3 SEAMCLP1 State

플러그인 상태 포맷을 별도로 정의했다.

```text
Magic                 SEAMCLP1
Byte order            Little-endian
Maximum size          256 MiB
Maximum channels      8
Maximum duration      10 minutes
Integrity             SHA-256
PCM                    Interleaved Float32
```

저장 내용:

- 원본 Sample Rate
- Channel Count
- Frame Count
- Master Gain
- UTF-8 Title byte sequence
- Pre-rendered Multichannel PCM
- 전체 선행 byte의 SHA-256

다음을 거부한다.

- 손상된 Magic
- 지원하지 않는 Version
- 잘린 Header/Payload
- Header와 Payload 크기 불일치
- 과도한 Channel/Duration/State Size
- Frame × Channel overflow
- NaN/Inf PCM
- SHA-256 불일치

State stream은 Host가 짧은 chunk 단위로 읽거나 쓰더라도 끝까지 처리한다.

### 2.4 Active State Load 정책

활성화된 플러그인에 대형 PCM 상태를 즉시 교체하면 audio thread와 상태
메모리 사이에 race가 생길 수 있다. Phase 10은 이를 숨기지 않는다.

```text
Inactive State Load   허용
Active State Load     거부
Host request_restart  호출
```

동적 Host Smoke에서 active load가 실패하고 restart 요청이 정확히 1회
발생하는 것을 검증했다.

### 2.5 Sample-rate 변환

State의 원본 sample rate와 Host sample rate가 다르면 `activate()` 단계에서
main-thread linear resampling을 수행한다. `process()` 내부에서는 resampling을
수행하지 않는다.

```text
State Load
→ Inactive Source PCM
→ Plugin Activate(host sample rate)
→ Prepared PCM
→ Start Processing
```

### 2.6 Audio-thread 계약

`process()`에서 하지 않는 작업:

```text
파일 I/O
Project/Voicebank Parsing
State Decode
Sample-rate 변환
음소 합성
메모리 할당
Mutex
로그 출력
Character 처리
```

`process()`는 다음만 수행한다.

```text
Host Transport → Source Frame 계산
Parameter Event 적용
Prepared PCM 읽기
Gain 적용
Planar Host Buffer에 복사
범위 밖 Frame은 Zero-fill
```


## 3. WAV ↔ CLAP State 도구

Standalone에서 Export한 실제 1~8채널 WAV를 플러그인 State로 연결하기 위해
`seam_clap_state_tool`을 추가했다.

```text
seam_clap_state_tool pack INPUT.wav OUTPUT.seamclapstate [--title TEXT] [--gain-db VALUE]
seam_clap_state_tool inspect INPUT.seamclapstate
seam_clap_state_tool extract INPUT.seamclapstate OUTPUT.wav
```

`pack`은 기존 bounded WAV parser를 사용해 PCM을 읽고 `PluginSession` 검증을
통과한 경우에만 durable atomic state 파일을 생성한다. `inspect`는 상태를
전체 decode·검증한 뒤 채널, sample rate, frame 수와 gain을 출력한다.
`extract`는 검증된 PCM을 다시 multichannel PCM16 WAV로 내보낸다.

동적 Host Smoke도 내부 diagnostic object가 아니라 이 `pack` 경로가 만든
State를 사용한다. 따라서 다음 실제 연결을 검증한다.

```text
Project SEAM WAV Export
→ seam_clap_state_tool pack
→ SEAMCLP1
→ ProjectSEAM.clap
→ Host Playback
```

## 4. first-party 동적 Host 검증기

`seam_clap_host`를 추가했다. 테스트가 정적 라이브러리를 직접 호출하는 것이
아니라 운영체제 동적 로더로 실제 `.clap` 파일을 연다.

```text
Linux      dlopen / dlsym
Windows    LoadLibrary / GetProcAddress
```

검증 흐름:

1. `clap_entry` 조회
2. Entry init
3. Plugin Factory 조회
4. Descriptor 검증
5. Plugin Instance 2개 생성
6. 37-byte 단위 partial input stream으로 State Load
7. 4채널 Audio Port 조회
8. Activate/Start Processing
9. Reference block 처리
10. frame 128의 -6 dB automation block 처리
11. 정지 Transport silence 검증
12. 29-byte 단위 partial output stream으로 State Save
13. State round-trip decode
14. Active State Load 거부와 restart 요청 검증
15. Stop/Deactivate/Destroy/Entry deinit

실제 결과:

```json
{
  "pluginId": "com.project-seam.render-player",
  "channels": 4,
  "framesProcessed": 256,
  "automationRatio": 0.501187,
  "stateRoundTrip": true,
  "activeLoadRejected": true,
  "restartRequests": 1,
  "transportPauseSilence": true
}
```

## 5. CLAP 의존성 및 라이선스

공식 CLAP 1.2.10 public C ABI에서 Phase 10에 필요한 선언만 기계적으로 합친
header subset을 저장소에 포함했다.

```text
Upstream revision
195b42a004144fab0b3cf95e9c067187d15365b7

License
MIT

Vendored header SHA-256
f388cfa5a8d33ba39c8bd26077b6c0c7e5c70b6795aa4418526f04e615dd21f0
```

기록 위치:

```text
third_party/clap/
third_party/manifest.yml
THIRD_PARTY_NOTICES.md
SBOM.spdx.json
```

## 6. 캐릭터 통합 경계

Character 01은 Official Voicebank와 Standalone 제품 화면을 대표하는 선택형
제품 아바타다. Phase 10 플러그인 DSP에는 들어가지 않는다.

포함되지 않는 영역:

```text
CLAP State
Audio Port Layout
PCM Payload
Process Callback
Host Transport
Master Gain Automation
State SHA-256 Identity
```

따라서 Character 01을 숨기거나 교체하거나 제거해도 플러그인 출력 byte와
State Identity는 바뀌지 않는다.

## 7. 테스트 및 검증

```text
Named Tests                     128 PASS / 0 FAIL
Debug CTest                      20 / 20 PASS
Release CTest                    20 / 20 PASS
ASan + UBSan Named Tests        128 PASS / 0 FAIL
ASan + UBSan Phase 10 CTest       6 / 6 PASS
Dynamic Linux Module Load        PASS
clap_entry Export                PASS
Master-only Branch Policy        PASS
Dependency / License Audit       PASS
Git Object Integrity             PASS
```

Release State Codec 회귀 기준:

```text
Channels              4
Sample Rate      48,000 Hz
Duration              1 second
Iterations            8
State Bytes      768,123
Encode Total      28.8035 ms
Decode Total      30.5687 ms
```

이는 현재 Linux 검증 환경의 회귀 자료이지 모든 Host와 CPU에 대한 성능
보장 수치는 아니다.

## 8. 정확한 완료 경계

Phase 10에서 완료된 것:

```text
실제 loadable Linux CLAP module
Plugin Factory / Descriptor
1~8채널 output port
SEAMCLP1 persistent state
Host transport playback
Sample-accurate Master Gain
Partial stream state I/O
WAV pack / state inspect / WAV extract CLI
First-party dynamic host smoke
Bounded and checksum-protected state
```

아직 완료로 처리하지 않은 것:

```text
CLAP GUI Extension
DAW 내부 Piano Roll / Phoneme / Unit Editor
Host 내부 비동기 가창 렌더 서비스
Live Note-event Singing Synthesis
macOS .clap Bundle Packaging
광범위한 Third-party Host 인증
VST3
AU
Code Signing / Notarization / Installer
계약·녹음·권리 확보가 끝난 인간 Voicebank
```

현재 결과는 전체 편집기를 DAW 안에 넣은 것이 아니라, 이후 GUI와 비동기
렌더 서비스를 올릴 수 있도록 수명주기·상태·Transport·Automation·RT 경계를
실제로 검증한 첫 플러그인 Vertical Slice다.
