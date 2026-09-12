# Project SEAM 개발 방향 심층 리뷰

> **2026-09-09 재검토로 주요 결론 정정됨.** 이 문서는 이전 실행 시점의 기록으로 보존한다. 아래의 canonical Phase 12C 수치는 실제 CLAP binary 실행 증거가 아니며, multilingual context 보호에도 회귀가 확인되었다. 현재 작업 트리에는 미통합 ABI 테스트로 인한 build 실패도 있다. 최신 판정·직접 재현·코드별 근거는 프로젝트 루트의 [심층 재검토](../../DEVELOPMENT_DIRECTION_DEEP_REVIEW_2026-09-09_KO.md)를 참조한다.

검토일: 2026-09-09  
검토 대상: `codex/production-readiness-completion` 현재 작업 트리  
판정 기준: 승인된 Full-Scope Beta GO 계획, `full-product-beta-contract.json`, 실제 C++ 진입점·런타임·테스트·릴리스 게이트

## 1. 결론

현재 개발 방향은 **아키텍처 관점에서는 올바르다**. 최근 작업은 화면에 버튼을 추가하거나 PASS 문자열을 만드는 방향이 아니라, 다음 공통 경계를 실제 코드에 연결했다.

- 언어별 발음 서비스 → 검색/가사·노트 편집/발음 힌트/기술 편집/성능 테이크/복사된 렌더 편집
- 실제 CLAP 번들·Voicebank 파일 → Phase 12C matrix/soak 실행 → plugin/bank/source/build identity
- 입력 크기·파일 링크·중복 JSON·stale revision → fail-closed 오류
- Full-Product report → JSON Schema뿐 아니라 R1–R20, 83개 case, check/operation/reviewer/raw-artifact 의미 검증

다만 이것은 **Beta GO에 근접했다는 뜻이 아니다**. 현재 상태는 “코드 경계와 검증 골격이 강해진 개발 후보”이지, 실제 출시 가능한 전체 가상 가수 제품이 아니다. 가장 큰 미완성 항목은 코드가 아니라 실제 음성 콘텐츠·신경망 모델·독립 청취/창작자 검증·설치된 호스트 증적이다.

따라서 방향 판정은 다음과 같다.

| 영역 | 판정 | 근거 |
|---|---|---|
| 핵심 C++ 구조 | 적절 | immutable snapshot, revision guard, 공통 resolver, bounded process/resource 경계가 실제 진입점에 연결됨 |
| 최근 multilingual 작업 | 적절 | English/Korean이 검색·편집·reconciliation까지 같은 resolver를 사용하며 Japanese fallback을 렌더 경로에서 사용하지 않음 |
| Phase 12C 큰 단계 | 적절하지만 아직 증적 단계 | canonical plugin/bank를 실제 로드·rehash하고 336 matrix/5초 smoke를 통과시킴 |
| 제품 완성도 | 미완성 | 실제 release-quality bank, neural model, full qualification, target host evidence가 없음 |
| 릴리스 판단 | 정확히 NO-GO | full-product profile/resource matrix가 UNRESOLVED이고 full report/validator/full soak/설치 증적이 없음 |

## 2. 실제로 확인한 구현 상태

### 2.1 언어 서비스 경계

`libs/seam-phonemizer/include/seam/phonemizer/language_resolver.hpp`에 English/Korean/Japanese를 선택하는 공통 resolver와 언어별 phone-hint validator가 있다. `language_resolver.cpp`는 모든 언어에 같은 입력 한도를 적용한다.

- 최대 notes/lyrics: 10,000
- aggregate lyric/hint 입력: 65,536 bytes/scalars 경계
- phoneme overrides: 4,096
- 중복 ID, malformed Unicode, invalid note는 adapter 호출 전에 거부

이 resolver는 다음 코드에서 실제 사용된다.

- `libs/seam-editor-ui/src/note_search_model.cpp`: GeneratedPhoneme/PronunciationDiagnostic 검색
- `libs/seam-application/src/lyric_commands.cpp`: 가사 변경·phoneme reconciliation·identity refresh
- `libs/seam-application/src/note_commands.cpp`: note 추가/삭제/geometry 변경과 언어별 hint inventory
- `libs/seam-application/src/performance_commands.cpp`: proposal/accept 시 pronunciation identity 검증
- `libs/seam-authoring-runtime/src/technical_edit_controller.cpp`: 기술 편집 review/rebind
- `libs/seam-phonemizer/src/pronunciation_resolver.cpp`: 복사된 phoneme/unit/seam edit의 context transfer

이 연결은 방향상 중요한 개선이다. 기존에는 English/Korean adapter가 있어도 편집 명령이 Japanese resolver만 호출할 수 있었고, 그 경우 화면에서는 지원하는 것처럼 보여도 저장·undo·rebind 시 다른 규칙이 적용될 수 있었다.

혼합 언어 region은 production resolver가 거부한다. 기술 편집 review는 이 경우에만 명시적인 diagnostic Japanese index를 사용해 호환 note와 비호환 note를 분리한다. 이 fallback은 렌더/identity publication에 사용되지 않으며, review UI의 retained-edit 위치를 보여 주기 위한 예외다.

### 2.2 Canonical Phase 12C vertical slice

이번 큰 단계에서 확인한 실제 흐름은 다음과 같다.

1. CMake의 `seam_clap_editor_plugin` target을 canonical CLAP target으로 선택한다.
2. Phase 12C runner가 `ProjectSEAMEditor.clap` bundle과 production-bank root를 인자로 받는다.
3. `phase12c/src/canonical_evidence.cpp`가 plugin tree와 bank tree를 no-symlink 방식으로 rehash한다.
4. `VoicebankCatalog`가 bank manifest를 읽고 `buildTrustedResource`로 실제 WAV/marker를 디코드한다.
5. matrix/soak가 embedded fixture가 아니라 production resource를 `seam::live_voice::VoiceEngine`에 publish한다.
6. 결과 JSON에 plugin ID/hash, bank ID/version/tree hash, sourceCommit, buildId를 기록한다.
7. `scripts/verify_phase12c_canonical_contract.py`가 동일 identity를 다시 확인한다.

현재 실행 결과:

- canonical 336-case matrix: PASS
- canonical 5-second smoke soak: PASS
- smoke workload: 32 active voices, 0 event overflow, finite output, resource clear/publish, MIDI/expression, steal, transition fallback
- transition fallback을 성공 workload로 인정한 이유: 현재 기술 bank에는 exact transition unit이 없고, fallback behavior 자체가 명시된 엔진 경로이기 때문

이것은 이전의 “fixture 결과를 canonical이라고 이름만 바꾸는” 상태보다 훨씬 안전하다. 다만 plugin을 직접 호스트하여 336 case 각각을 실행하는 완전한 installed-host evidence와는 아직 다르며, 5초 smoke는 7,200초 release soak를 대체하지 않는다.

### 2.3 Full-product gate

`tools/external_beta/full_product_report.py`와 `release_gate.py`의 현재 방향도 적절하다. report가 제공되면 다음을 실제로 검사한다.

- candidate root와 acceptance/full-contract digest binding
- JSON duplicate key, non-finite number, depth/size, symlink/file boundary
- R1–R20와 83개 canonical case의 정확한 coverage
- language/resource/backend/platform/host dimension coherence
- artifact kind, reviewer role 분리, measurement/check/operation coverage
- empirical cell identity와 raw evidence locator

중요한 점은 report가 없으면 이전의 “semantic validator unavailable” 진단을 유지하고, report가 있어도 unresolved criteria를 자동 PASS시키지 않는다는 것이다. 이것은 출시 게이트로서 올바른 fail-closed 동작이다.

## 3. 검증 결과

### 3.1 최신 실행 결과

| 검증 | 결과 |
|---|---:|
| Release 전체 CTest | 107/108 PASS |
| 유일한 전체 실패 | `seam_tracked_source_closure` |
| source-closure 미색인 입력 | 264개 |
| Release `seam_tests` | 695 PASS / 0 FAIL |
| Phase 12C canonical slice | 7/7 PASS |
| Debug multilingual 영향 suite | 5/5 PASS |
| Release multilingual 영향 suite | 5/5 PASS |
| Full-product/report/canonical Python focused tests | 23 PASS |
| `git diff --check` | PASS |
| official `clap-validator` | NOT_RUN (로컬 명령 미설치, exit 3) |

source-closure 실패는 현재 코드 테스트 실패가 아니라, 작업 트리의 신규/수정 파일 264개가 Git index에 아직 올라가지 않은 상태를 정확히 보고한 것이다. 그러나 실제 release candidate에서는 반드시 해결해야 한다. 이 작업에서는 사용자의 명시적인 commit/push 요청이 없는 상태이므로 자동 staging으로 이 검증을 속이지 않았다.

### 3.2 현재 build identity의 한계

canonical runner 결과에는 `sourceCommit=000000...000` 및 `buildId=0.13.1-local`이 기록된다. 이는 로컬 개발 build identity로는 유효하지만 release provenance가 아니다. 따라서 strict canonical verifier가 matrix/soak 구조를 PASS로 판정하는 것과, release candidate가 승인되는 것은 별개의 문제다. 실제 release에서는 source commit, build epoch, signed installed tree identity를 공급해야 한다.

## 4. 아직 Beta GO를 막는 핵심 문제

### P0 — 실제 제품을 만들지 못하게 하는 blocker

1. **출시 가능한 Voicebank가 없다.** 현재 `assets/demo-human-voicebank-public-domain/production-bank/README.md`는 해당 bank가 기술 fixture이며 Official Voicebank 01이 아니고, contracted singer가 없으며 release-quality singing에 적합하지 않다고 명시한다. `manifest.json`에는 8개 unit이 있고 여러 unit이 동일한 짧은 WAV를 공유한다. 이는 60 phrases/language, range/style, unseen lyric acceptance를 만족하는 singer resource가 아니다.
2. **Neural singer가 없다.** `libs/seam-neural-synthesis`에는 bounded worker protocol/model contract/shape validation이 있지만 실제 학습 checkpoint, ONNX model, vocoder, vocabulary package, rights record, CPU qualification 결과가 없다.
3. **English/Korean은 bootstrap rule/lexicon이다.** native-speaker review, complete dictionary, consonant/transition coverage, matching bank, listener acceptance가 없다.
4. **독립 음악·언어·creator acceptance가 없다.** 최소 5 creators, native-language reviewer, fixed corpus, 3 complete songs, counterbalanced assisted/manual task, acoustic/listener rubric의 raw records가 없다.
5. **설치된 platform/DAW 증적이 없다.** Windows x64, macOS installed release, VST3/AUv2, REAPER/Bitwig/Logic 등 required host tuple의 실제 installed-byte/session/bounce evidence가 없다.

### P1 — 구현은 있으나 qualification이 닫히지 않은 영역

1. Phase 12C official clap-validator 0.4.1은 로컬에서 NOT_RUN이다.
2. 정확한 7,200초 full soak는 실행되지 않았다. smoke는 개발 회귀용이다.
3. U34 Follow Host는 전체 tempo-map capture/host tuple qualification이 남아 있다.
4. USTX/SMF는 bounded subset/interchange lifecycle이며, 전체 OpenUTAU/DAW interoperability proof가 아니다.
5. advanced expressions는 capability/DSP 계약이 일부 구현됐지만, 모든 promised control의 measured/listener property qualification은 없다.
6. character state binding은 runtime state selection 구조가 있지만 production asset rights, mouth/phoneme synchronization, installed asset acceptance가 없다.

### P2 — 출판/운영 blocker

1. source-closure 264개 미색인 파일은 clean checkout 재현성을 막는다.
2. 최종 full-product report가 없고 evaluation profile/resource matrix가 `UNRESOLVED`다.
3. candidate root, signed archive, installed descendant, independent approvals가 없다.
4. 현재 dirty tree는 과거 U60/support 변경과 이번 변경이 섞여 있으므로, release 전에 ownership별 commit/clean candidate freeze가 필요하다.

## 5. 방향이 올바르지만 조정해야 할 운영 원칙

### 유지해야 할 것

- 모든 새 기능은 immutable input identity → bounded execution → stale rejection → atomic publication 순서로 구현한다.
- Fixture는 API/경계 테스트에만 사용하고, 실제 singer/model/rights/installed evidence로 승격하지 않는다.
- language/resource/backend를 한 곳의 resolver와 identity contract에서 선택한다.
- release gate는 부족한 evidence를 PASS로 추론하지 않는다.
- UI 개선도 실제 source/model/runtime acceptance와 연결된 경우에만 Beta 진척으로 계산한다.

### 피해야 할 것

- smoke duration을 full soak처럼 보고하기
- public-domain 한 개 음성 샘플을 여러 phoneme label로 복제해 “voicebank complete”라고 부르기
- English/Korean bootstrap lexicon을 native-quality pronunciation이라고 부르기
- protocol test PASS를 neural quality PASS로 해석하기
- source-closure를 통과시키기 위해 무관한 파일을 무검토 staging하거나 history를 다시 쓰기
- unresolved evaluation criteria를 구현 난이도만으로 삭제·완화하기

## 6. “빅스텝”의 올바른 다음 순서

다음 큰 단계는 모든 것을 한 번에 만든다는 의미가 아니라, 가장 많은 blocker를 동시에 줄이는 **하나의 실제 vertical slice**여야 한다.

### Big Step A — 실제 일본어 production bank vertical slice

1. 합법적인 단일 여성 화자 또는 명시적으로 허가된 생성 음성 source를 선정하고 rights/provenance record를 먼저 고정한다.
2. recording inventory를 60 phrase/language 요구와 실제 supported range/style matrix에 맞춰 만든다.
3. source take → marker/pitch/F0/QC → retake/review → candidate export → install receipt를 실제 WAV로 연결한다.
4. 기존 `LiveResourceBuilder`, production renderer, standalone/CLAP canonical runner를 같은 bank triple에 연결한다.
5. unseen Japanese song을 save/reopen/undo/export/installed resolve까지 실행하고 raw audio/measurement/reviewer evidence를 남긴다.

이 slice가 완료되면 R4/R5 일부, R7 Japanese, R12 일부, R13 일부, R15 일부, R20의 실제 연결이 동시에 검증된다. 지금의 demo fixture는 이 slice의 대체물이 아니다.

### Big Step B — U35/U37 neural path를 실제 model-ready 상태로

1. `tools/voice_model_training/`에 dataset manifest, speaker/session split, alignment validation, resumable checkpoint identity, export manifest를 만든다.
2. 권리 없는 corpus가 pipeline에 들어오지 않도록 source admission을 연결한다.
3. 작은 CPU model pilot로 request→helper→PCM→render snapshot까지 연결하되, pilot PASS를 singer qualification으로 표시하지 않는다.
4. 실제 model이 생긴 뒤에만 U36 acoustic/listener criteria를 채우고 U42 resource matrix에 편입한다.

### Big Step C — evidence/installed closure

1. release build identity를 실제 source commit/build epoch로 생성한다.
2. source-closure를 clean index에서 PASS시킨다.
3. official clap-validator 0.4.1, full soak, installed macOS/Windows, VST3/AUv2와 required host tuple을 실행한다.
4. 모든 raw artifact를 U45 report에 바인딩하고, U46 restored audit로 재생성한다.

## 7. 최종 go/no-go 판정

현재 판정은 **NO-GO, 그러나 방향은 GO**다.

- “개발 방향이 맞는가?” → 예. 최근 구조 변경은 실제 병목을 해결하고 있으며 테스트만 늘리는 방향은 아니다.
- “현재 코드가 기술 preview를 만들 수 있는가?” → 예. bounded Japanese/sample path와 English/Korean bootstrap path, canonical Phase 12C engineering slice가 동작한다.
- “현재 제품을 HATSUNE MIKU급 또는 Beta GO라고 부를 수 있는가?” → 아니오. 실제 voicebank/model/quality/rights/installed-host/independent evidence가 없다.
- “다음 빅스텝을 진행해야 하는가?” → 예. 다만 다음 목표는 또 다른 fixture나 PASS flag가 아니라, 실제 source에서 설치된 bank와 unseen song까지 가는 vertical slice여야 한다.

이 문서는 방향을 재설정하는 문서가 아니라, 승인된 Full-Scope 범위를 유지하면서 코드 구현과 실제 콘텐츠·검증 작업의 경계를 명확히 하는 현재 기준이다.
