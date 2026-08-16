# Project SEAM Phase 1 구현 보고서

## 1. 결과 요약

Phase 1은 문서나 클래스 이름만 배치한 스캐폴딩이 아니라, 다음 흐름을 실제 실행 가능한 C++20 코드로 구현하는 것을 완료 기준으로 삼았다.

```text
프로젝트 생성
→ 10,000개 음표 구성
→ 피아노롤 가시 영역 질의
→ 다중 음표 이동
→ Undo / Redo
→ JSON 저장
→ JSON 재로드
→ 도메인 동등성 확인
→ SVG 피아노롤 렌더
→ 오디오 콜백 시뮬레이션
```

현재 저장소는 `master` 브랜치 하나만 사용한다. 신규 브랜치를 생성하지 않았으며, Git hook과 CI 검증 스크립트로 해당 정책을 고정했다.

## 2. Phase 1 요구사항 대비 구현 상태

| 요구사항 | 상태 | 구현 위치 |
|---|---|---|
| C++20·CMake 프로젝트 | 완료 | `CMakeLists.txt`, `CMakePresets.json` |
| 모듈 경계 | 완료 | `libs/seam-*` |
| Tick | 완료 | `seam/time/tick.hpp` |
| TempoMap | 완료 | `tempo_map.*` |
| MeterMap | 완료 | `meter_map.*` |
| Tick↔Seconds↔Sample Frame | 완료 | `TempoMap` |
| Project/Track/Region/Note | 완료 | `seam/domain` |
| Lyrics 분리 모델 | 완료 | `LyricToken` |
| Strong ID | 완료 | `seam/core/id.hpp` |
| 로드 후 ID 충돌 방지 | 완료 | `ProjectFactory::synchronizeWith` |
| Command | 완료 | `ICommand`, `CompositeCommand` |
| Undo/Redo | 완료 | `EditorSession` |
| Project Revision | 완료 | `EditorSession::revision` |
| 음표 추가 | 완료 | `AddNoteCommand`, `drawNote` |
| 음표 이동 | 완료 | `MoveNotesCommand` |
| 음표 리사이즈 | 완료 | `ResizeNotesCommand` |
| 음표 삭제 | 완료 | `RemoveNotesCommand` |
| 다중 선택 | 완료 | `SelectionModel` |
| Box Selection | 완료 | `PianoRollModel::selectInBox` |
| Snap/Quantize | 완료 | `Quantizer` |
| Timeline Zoom/Pan | 완료 | `TimelineTransform` |
| Piano Keyboard 좌표 | 완료 | `PitchTransform`, SVG keyboard |
| Hit Test | 완료 | `PianoRollModel::hitTest` |
| 10,000 Note Virtualization | 완료 | `NoteSpatialIndex` |
| 직렬화 초안 | 완료 | 자체 JSON + `ProjectJsonCodec` |
| 임시 파일 기반 Save | 완료 | 임시 파일 작성 후 replacement |
| UTF-8 가사 | 완료 | UTF-32↔UTF-8 변환 |
| 오디오 콜백 계약 | 완료 | `IAudioProcessor`, simulator |
| 네이티브 iPlug2+Skia Shell | 미통합 | 의존성 감사 후 adapter 단계로 보류 |
| 캐릭터 실루엣 3안 | 완료 | `assets/character-01/concepts` |
| 128px 식별 테스트 | 완료 | concept thumbnail/silhouette |
| Low-poly Blockout | 완료 | OBJ/MTL/PNG 3안 |
| 라이선스 Gate | 완료 | `tools/license-auditor/audit.py` |
| Master-only Gate | 완료 | Git hooks, script, CI |

## 3. 구현된 아키텍처

### 3.1 `seam_core`

플랫폼과 도메인에서 함께 쓰는 최소 기반을 제공한다.

- `Error`, `ErrorCode`
- 예외 없이 경계를 넘기는 `Result<T>`
- Strong typed `Id<Tag>`
- 충돌 방지를 위한 monotonic `IdGenerator`
- 로거 계약
- 해시 보조 함수

### 3.2 `seam_domain`

음악적 상태를 표현하며 그래픽·파일·운영체제 SDK를 알지 못한다.

- `Tick`, `TempoMap`, `MeterMap`, `Quantizer`
- `Project`, `VocalTrack`, `AudioTrack`, `VocalRegion`
- `Note`, `LyricToken`
- `VoicebankReference`, `CharacterReference`
- 전역 ID 유일성, 음표 범위, lyric 참조 정합성 검증

음표와 가사를 분리한 이유는 이후 한 음절이 여러 음표에 걸리는 melisma와 한 음표 안의 복수 phoneme을 지원하기 위해서다.

### 3.3 `seam_application`

편집 행위와 세션을 담당한다.

- Command apply/revert
- Composite rollback
- Undo/Redo stack
- Project revision
- Selection model
- Project/Track/Region/Note factory
- 로드된 프로젝트의 최대 ID를 관찰하여 신규 ID 충돌 방지

현재 구현된 명령은 Add, Move, Resize, Delete다. Delete 명령은 삭제된 Note와 더 이상 참조되지 않는 Lyric을 함께 보존하여 Undo 시 원본 상태를 복원한다.

### 3.4 `seam_editor_ui`

특정 그래픽 SDK와 분리된 피아노롤의 geometry·interaction model이다.

- musical tick과 pixel 변환
- pitch와 vertical pixel 변환
- viewport query
- note rectangle 계산
- hit test
- box selection
- draw/move/resize/delete
- 10,000-note spatial index
- SVG proof renderer

이 구조는 Phase 2에서 Skia painter와 native input adapter를 연결할 때 도메인과 interaction 규칙을 다시 작성하지 않기 위한 것이다.

### 3.5 `seam_formats`

Phase 1에서는 외부 JSON 라이브러리를 반입하지 않고 필요한 범위의 parser/writer를 구현했다.

- object, array, string, number, boolean, null
- UTF-8 문자열 처리
- schema/version 검증
- project round-trip
- 임시 파일 저장 후 staged replacement (전 OS crash-atomic 보장 아님)

현재 JSON은 개발·검증용 canonical draft다. 대형 automation data와 audio cache는 이후 binary chunk 또는 CBOR 계열로 분리할 예정이다.

### 3.6 `seam_platform`

Phase 1의 오디오 실시간 경계를 먼저 고정했다.

- `AudioProcessContext`
- `IAudioProcessor`
- deterministic `SilenceProcessor`
- preallocated callback simulator
- desktop backend contract와 headless backend

실제 audio device와 iPlug2 shell은 exact dependency revision을 고정한 뒤 adapter로 연결한다.

## 4. 실행 증거

`seam_phase1_demo`는 10,000개 음표를 가진 프로젝트를 생성한다. 현재 가시 viewport에는 76개 음표만 materialize된다. 즉, 전체 Note를 매 frame 그리는 구조가 아니다.

생성 파일:

```text
out/phase1/phase1-demo.seam.json
out/phase1/phase1-piano-roll.svg
out/phase1/phase1-piano-roll.png
out/phase1/phase1-summary.json
```

검증용 사본:

```text
docs/phase1/evidence/phase1-piano-roll.svg
docs/phase1/evidence/phase1-piano-roll.png
docs/phase1/evidence/phase1-summary.json
docs/phase1/evidence/phase1-benchmark.json
```

## 5. 테스트

현재 테스트는 다음 범주를 포함한다.

- 복수 tempo 구간의 tick/seconds/sample 변환
- 초기 tempo event 보호
- meter와 bar/beat 계산
- 음수 tick quantization
- Add/Move/Resize/Delete Undo/Redo
- 로드 후 ID reserve
- UTF-8 JSON parse
- project encode/decode 동등성
- 지원하지 않는 schema 차단
- anchor-preserving timeline zoom
- 10,000-note virtualization
- piano-roll draw/select/move/hit-test/delete
- deterministic audio callback

Phase 1 완료 시점 기준 총 16개 test case가 실행된다.

## 6. 캐릭터 Track

Phase 1에서 최종 캐릭터를 확정하지 않았다. 대신 이후 실제 저폴리곤 제작에서 비교 가능한 세 방향을 보존했다.

- Burgundy: 일상적 hoodie·canvas-shoe 실루엣을 유지하는 주 방향
- Violet: 장식 수를 줄인 최소 실루엣 대조군
- Teal: 소프트웨어 상태색을 실험하는 대조군

각 안에는 다음이 포함된다.

- 원본 concept image
- 128×128 thumbnail
- 128×128 silhouette
- 매우 낮은 triangle 수의 OBJ blockout
- Phase 2에서 제거·수정할 poser 위험 요소

Blockout은 최종 모델이 아니라 topology pipeline과 silhouette fixture다.

## 7. 의도적으로 완료했다고 주장하지 않는 부분

### 7.1 Production native shell

현재 저장소는 iPlug2와 Skia 소스를 vendoring하지 않았다. 따라서 native window, real mouse event dispatch, OS IME, GPU Skia canvas는 아직 실행되지 않는다. CMake option은 audit를 거치지 않은 dependency를 잘못 활성화하지 않도록 명시적으로 차단한다.

### 7.2 Singing synthesis

다음은 Phase 1 범위가 아니다.

- Phonemizer
- Voicebank Studio
- Unit selection
- Timing solver
- RawLoopRenderer
- PSOLA
- SeamComposer
- Phrase cache

현재 오디오 모듈은 합성 엔진이 들어갈 real-time contract만 구현한다.

### 7.3 Final character art

현재 이미지는 내부 방향 탐색 자료다. 공식 상업 캐릭터는 별도 2D turnaround, 실제 low-poly topology, first-party texture, 권리 계약, 외부 emo fan review 이후 확정해야 한다.

## 8. 다음 구현 순서

1. 승인된 exact iPlug2·Skia revision을 반입하고 native adapter를 구축한다.
2. native text overlay와 한글·일본어 IME를 연결한다.
3. Phonemizer API와 첫 언어 구현을 추가한다.
4. Voicebank manifest와 Sample Microscope를 구축한다.
5. Unit candidate/selection과 Timing Solver를 작성한다.
6. RawLoopRenderer 수직 슬라이스를 완성한다.

Phase 1의 목적은 이후 합성 엔진을 얹어도 시간 모델, 편집 명령, 저장 형식, UI geometry를 다시 폐기하지 않아도 되는 토대를 만드는 것이다.
