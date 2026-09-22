# SEAM: 진행도 재점검과 제품 완성을 위한 실행 계획

작성일: 2026-09-22  
기준: `ed41968dbb4f6b46d30956130e77d225087b2d7f` + 보존된 네이티브 가져오기 미커밋 변경  
성격: 코드·테스트 기반 분석 및 다음 개발 계획. 이 문서 작성으로 구현, 단위 승인 또는 출시 승인을 추가하지 않는다.

## 1. 결론

**음성 합성과 편집 기반은 상당히 구현됐지만, 제품 전체가 95% 완성되어 마지막 정리만 남은 상태는 아니다.** 남은 핵심은 실제로 쓸 만한 가수의 품질, 네이티브 앱에서 끊김 없이 완주하는 제작 과정, 실제 설치 제품의 검증이다.

그렇다고 처음부터 다시 만들 상황도 아니다. 원본 목소리 설계, 생성·입력 재료의 뱅크 제작, 설치된 절차적 가수의 약 39.5초 곡 렌더링, 표현 편집, 저장·재열기·내보내기, 신경망 학습·배포 기반이 존재한다. 앞으로는 개별 내부 기능을 계속 늘리는 것보다 **이 기반을 하나의 실제 제작 경험으로 완성하고 승인하는 것**이 우선이다.

이번 점검에서는 기존 계획보다 구체적인 결함도 발견했다. MIDI 해상도 변환 누락, 마이크 실패 시 무음 입력 대체, 변환 손실 보고 누락, 저장 확인 창의 문서 변경 보호 공백을 다음 완료 묶음에 포함해야 한다.

## 2. 현재 진행도: 무엇을 분모로 삼는가

| 지표 | 현재 값 | 정확한 의미 |
|---|---:|---|
| 로컬 승인 기록이 있는 구현 단위 | **6/48 = 12.5%** | U1–U5와 로컬 POSIX 범위 U29. 모든 플랫폼에 대한 6개 완전 승인은 아니다. |
| 위 로컬 집계에서 미승인인 단위 | **42/48** | 상당수가 부분 구현돼 있다. 42개를 처음부터 만들어야 한다는 뜻이 아니다. |
| 전체 제품 요구사항의 출시 승인 | **0/20 = 0%** | 전체 출시 후보에 대한 승인 기록 기준. 코드 구현률이 0%라는 뜻이 아니다. |
| 계약에 등록된 출시 리소스 | **0개** | 개발용 음원·뱅크·학습 모델은 있지만 출시 리소스 표는 비어 있다. |
| 평가 프로필의 미확정 항목 | **13개** | 음향·표현·발음·정체성 기준, 실행 예산, 기준 장비, 프로필 확정, 리소스 표 등. |
| 코드 구현량 또는 남은 노동량 기준 전체 진행률 | **산출 불가** | 단위별 크기와 부분 구현량을 측정하지 않았으므로 50%, 80%, 95% 등으로 단정하지 않는다. |

따라서 현재 제시할 수 있는 검증 가능한 숫자는 **“로컬 단위 승인 집계 12.5%”**다. 이를 “코드가 12.5%밖에 없다” 또는 “남은 시간이 87.5%다”로 해석하면 잘못이다. 최근 수정도 실제 성과지만 원래 단위 전체의 종료 조건을 만족하지 않았으므로 분자를 올리지 않았다.

승인 근거: [U1–U5 기록](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/FULL_SCOPE_BETA_EXECUTION.md:1321), [U29 로컬 승인과 집계 정정](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/INTEGRATED_SINGER_EXECUTION.md:415), [48개 단위 정의](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:213).

## 3. 이번에 직접 확인한 상태

### 코드 및 저장 상태

- 로컬 HEAD와 조회한 원격 `master`가 모두 `ed41968d`다.
- 레시피 기능 제한 수정 `62779c06`은 커밋·독립 리뷰·원격 반영이 끝났다. 스키마 1–11에서 실제 레시피 내용으로 유성 마찰음·폐쇄음 등의 허용 여부를 검사한다.
- 네이티브 가져오기 P2는 13개 추적 파일의 변경과 3개 신규 소스/테스트 파일로 남아 있다. 이번 분석은 이를 수정하거나 커밋·푸시하지 않았다.
- 기존 두 개발 기록과 실제 코드가 다른 경우 최신 코드를 우선했다. 예를 들어 캐릭터 입 모양 에셋은 현재 존재하므로 과거의 “입 모양 에셋이 없다”는 지적을 그대로 재사용하지 않았다.

### 이번 새로 실행한 검사

기존 빌드 바이너리를 재실행했다. 이번에 전체 소스 또는 신규 AppKit 변경을 다시 빌드한 것은 아니다.

| 검사 | 결과 | 해석 |
|---|---|---|
| Release 음성 설계 / Designer / 최종 출력 완전성 / 원본 가수 곡 / 포먼트 | **5개 CTest 대상 통과, 23.52초** | 해당 바이너리의 기능 회귀 증거. 신규 네이티브 가져오기 UI나 청취 품질 승인 아님. |
| Release SMF / USTX / interchange service | **3개 대상 실패, 내부 사례 51 통과·5 실패, 1.09초** | 실패 4개는 의도한 손실 보고 결함에 도달. 나머지 1개는 테스트 양성 대조군부터 실패. |
| 전체 제품 계약 정의 / reader / gate / report Python 테스트 | **44개 통과, 23.339초** | 검증기 동작이 맞는다는 증거이며 실제 제품 합격과 다름. |
| 현재 계약 프로필 검사 | **정의 오류 0, 미확정 13** | 요구사항 20개, 사례 83개, 출시 리소스 0개. `matrixStatus`/프로필은 `UNRESOLVED`, 증거는 `NOT_RUN`. |
| 추적 소스 완전성 검사 | **실패: 미등록 신규 파일 3개** | 새 review model 헤더·구현·테스트가 Git 인덱스에 없는 상태. 파일 유실이 아니라 진행 중인 변경의 미통합 상태. |
| `git diff --check` | **통과** | 공백 오류 검사일 뿐 기능 검증 아님. |

재실행 로그: [가져오기 검사](/tmp/seam-progress-interchange-2026-09-22.log), [가수·표현 검사](/tmp/seam-progress-capabilities-2026-09-22.log). `/tmp` 파일은 영구 보존 증거가 아니다.

이번에는 전체 CTest, Debug 재실행, 실제 AppKit 조작, 마이크 장치, 외부 DAW, 신경망 재학습, 독립 청취를 수행하지 않았다. 새 UI와 lifecycle 테스트는 작성되어 있지만 이번 검사로 실행 승인된 것으로 보지 않는다.

## 4. 즉시 처리할 결함

### A. MIDI의 PPQ가 다르면 음악적 길이가 달라진다

[SMF 변환기](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-interchange/src/smf_project_conversion.cpp:30)는 기본 프로젝트를 만들고 원본 note/tempo/meter tick 값을 그대로 복사한다. [프로젝트 생성기](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-application/src/project_factory.cpp:37)는 기본 960 PPQ를 사용한다.

따라서 480 PPQ 파일의 480 tick짜리 4분음표를 그대로 넣으면 SEAM에서는 반 박자가 된다. 이것은 소스에서 확인한 변환 누락이며, 이번에 실제 앱으로 재생 비교한 결과는 아니다.

**수정 방향:** 하나의 검사된 시간 변환 규칙으로 음표 시작·끝, 가사, 템포·박자 이벤트와 region 범위를 함께 변환한다. 480/960/1920 PPQ 및 정수 배율이 아닌 경우를 검사한다. 음표 길이 소실, 반올림 충돌, 범위 초과를 숨기지 않는다.

### B. 실제 마이크 실패가 무음 입력의 성공으로 바뀔 수 있다

[Studio 입력 초기화](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:1722)는 실제 장치 열기에 실패해도 `createThreadedSilenceInputDevice()`를 만들고 성공을 반환한다. 이후 [일반 녹음 경로](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:1634)는 이를 시작하고 WAV 저장·검사·가져오기 경로로 진행할 수 있다.

오류와 backend 표시가 있으므로 완전히 숨겨진 대체라고 단정하지 않는다. 그러나 일반 모드에서 장치 실패를 합성 무음으로 대체하는 것은 [U22의 명시적 완료 조건](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:604)과 충돌한다. 실제 장치 장애는 이번에 재현하지 않았으며 출시 QC까지 통과했다는 주장도 아니다.

**수정 방향:** 무음 장치는 명시적 테스트 모드에만 허용한다. 일반 모드에서는 권한 거절·장치 열기 실패·분리 상태를 복구 가능한 실패로 유지하고, 성공한 실제 녹음으로 저장하거나 표시하지 않는다.

### C. 변환 손실 보고가 누락된다

- [USTX parser](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-interchange/src/ustx_codec.cpp:490), [USTX converter](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-interchange/src/ustx_project_conversion.cpp:20), [공통 service](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/src/interchange_service.cpp:59)가 최대 4,096개 이후의 항목을 버릴 수 있다.
- [SMF decoder](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-interchange/src/smf_codec.cpp:155)는 일반 및 running-status 경로에서 지원하지 않는 CC·프로그램 변경·pitch bend·pressure를 손실 항목 없이 무시한다.
- 이번 실패 중 USTX parser/converter/export와 MIDI channel-control 사례가 이 문제를 직접 드러냈다. UI가 반환된 행을 모두 보여도 이미 버려진 내용은 복원할 수 없다.

**수정 방향:** 제한 이내의 보고서는 완전하게 보존한다. 제한을 초과하면 메모리 상한을 유지하면서 가져오기 승인 또는 출력 파일 쓰기 전에 명시적으로 거부한다. 제한을 없애거나 조용히 잘라 성공시키지 않는다. MIDI의 두 파싱 경로 모두 동일한 손실 정책을 사용한다.

### D. 문서 변경 보호가 저장 확인 창 이전까지 이어지지 않는다

신규 [문서 stamp 검사](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-standalone/src/application_controller.cpp:828)는 파일 선택과 변환 검토 중 변경을 막지만, stamp는 [저장 확인 뒤에 생성된다](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-standalone/src/application_controller.cpp:1099).

[저장/폐기 확인](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-standalone/src/application_controller.cpp:766) 또는 Save As modal 중 다른 편집이나 문서 교체가 발생하면 그 변경이 이전 승인에 포함될 수 있다. 소스 수준에서 확인한 보호 공백이며 실제 UI 재현은 남아 있다.

**수정 방향:** 파괴적 전환의 시작부터 확인한 문서 identity/revision을 보호한다. 의도한 정상 저장으로 바뀌는 identity와 외부 편집·교체를 구분한다. 취소 시 새 변경을 되돌리거나 덮어쓰지 않는다.

### E. 새 테스트 하나는 먼저 바로잡아야 한다

[MIDI service 상한 테스트](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_interchange_service.cpp:30)는 비어 있는 `projectName`을 전달한다. 변환기는 이를 거부하므로 4,096개 정상 입력 확인부터 실패하고 4,097개 초과 분기에는 도달하지 않는다.

**수정 방향:** 유효한 이름을 가진 양성 대조군을 먼저 통과시킨 뒤 초과 거부를 검증한다. 현재 실패를 “service overflow 재현 성공”으로 세면 안 된다.

## 5. 기능별로 실제 남은 차이

| 영역 | 이미 있는 것 | 끝내야 하는 것 |
|---|---|---|
| 녹음 없이 목소리 만들기 | 발성·공명·자음 계열·스타일 레시피, Designer 편집·미리듣기·저장 | 원하는 여성 가수 정체성, 자음 명료도·음역·스타일 품질, UI에서 뱅크·곡까지 완주 |
| 실제 녹음/외부 음원으로 뱅크 만들기 | 입력·편집·검사·재녹음·패키징·설치 서비스와 연결 테스트 | 실제 장치 실패 처리, 진짜 입력을 사용한 네이티브 완주, 실제 제작 리소스 승인 |
| 노래 편집·출력 | 약 39.5초 곡, tuning, undo/redo, 저장·재열기, 설치 가수 렌더링 | 처음 보는 가사로 도움 없이 작업하는 사용자 검증과 음악적 품질 |
| 클래식·절차적 표현 | breathiness/tension/growl/formant 및 여러 소유권·캐시·출력 경계 | 실제 가수/스타일/렌더러 조합의 지원 표와 음악적 유효성 승인 |
| 신경망 가수 | 학습·데이터·ONNX·native worker·렌더링 기반과 실제 후보 | 무성 자음의 약함/주기성 문제, 학습 외 자료 성능, 실제 모델 배포 품질·실행 예산 |
| 일본어·영어·한국어 | 일본어 읽기 검증, 영어 강세·음절, 한국어 분해·문맥 규칙 | 영어 26개 초기 사전의 일반 어휘 확장, 한국어 사전 의존 예외, 실제 가수의 언어별 발음 승인 |
| 자동 표현·테이크·화음 | 제안·부분 수락·취소·실행 취소·화음 기반 | 현재 기본 생성기의 4개 채널 밖 필수 생성 기능, UI 비교/재생성, 실제 창작 효용 |
| 캐릭터 | 입 모양 에셋과 렌더링 cue 연동 | 공식 출시용 자산·정체성·권리 증거와 최종 UI 사용성 |
| 출시/검증 | 계약·증거 검증기·복구·호스트/패키징 기반 | 실제 리소스 등록, 기준 확정, 서명·설치·호스트·지원 및 정확한 후보 감사 |

곡 결과는 [소스에서 직접 `releaseEligible=false`로 기록](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_original_singer_song_journey.cpp:890)한다. 청취는 `NOT_REVIEWED`, 창작자 사용은 `NOT_OBSERVED`다.

신경망은 “추론 코드가 없다”가 아니라 **실제 후보의 품질 승인이 없다**가 정확하다. [46개 비교 자료의 결정 기록](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/listening/2026-09-22-stage-localization/decision.md:1)은 실패한 음향 기준과 미검토 청취를 유지한다. 또한 [선택적 neural 테스트](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_neural_production_render.cpp:138)는 환경 변수 없이 반환할 수 있으므로 단순 suite PASS를 실제 모델 추론 증거로 바꾸면 안 된다.

언어와 생성기의 한계는 [영어 사전](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-phonemizer/src/english_phonemizer.cpp:25), [한국어 예외 설명](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-phonemizer/src/korean_phonemizer.cpp:94), [자동 표현 제한](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-synthesis/src/automatic_performance.cpp:161)에 명시돼 있다. [캐릭터 dossier](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/phase13b/character-01-dossier.json:6)는 아직 개발 전용이다.

## 6. 마무리 계획: 네 개의 큰 완료 묶음

기존 R1–R20/U1–U48을 바꾸지 않는다. 아래 묶음은 실행 순서이며, 새 진행률 분모가 아니다. [직전 P1–P7 계획](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/SEAM_COMPLETION_PLAN_2026-09-22_KO.md)의 P1은 완료됐고, 나머지를 연결된 결과 중심으로 묶었다.

### 묶음 1 — 음악과 사용자 작업을 잃지 않는 입력 경로

대상: 기존 P2, U22 입력 실패 경계, U29–U32 관련 작업.

1. E의 테스트 fixture를 먼저 고쳐 정상/초과 입력이 서로 다른 경로를 검증하게 한다.
2. C의 보고서 상한과 MIDI channel-event 손실 처리를 수리한다.
3. A의 PPQ 변환을 음표·가사·템포·박자 전체에 일관되게 구현한다.
4. D의 저장 확인·Save As·파일 선택·검토 단계에 문서 변경 보호를 연결한다.
5. B의 일반 녹음 실패와 명시적 테스트 입력을 분리한다.
6. 현재 작성된 review model/AppKit 연결을 strict Release/Debug로 빌드하고 lifecycle/model/codec/service 테스트를 실행한다.
7. 실제 macOS에서 Import → 손실 검토 → 취소/수락 → 새 미저장 문서 → Save As → 재열기를 수행한다. 원본 해시 보존, Return/Escape, 긴 Unicode, 마지막 보고 항목, 작은 화면·키보드·포커스를 확인한다.

**완료 산출물:** 재현된 실패와 수정 뒤 통과 기록, 정상 MIDI 길이 비교, 실제 앱 조작 기록, 녹음 실패 복구 테스트, 독립 리뷰가 끝난 통합 커밋.

**별도 남길 경계:** embedded editor 연결, 실제 OpenUtau/DAW 교환, Windows 실행. standalone dialog 완료만으로 U32 전체를 승인하지 않는다.

첫 통합 시 사용할 검사 명령은 다음과 같다. 기존 빌드 설정을 확인하고 소스 편집이 끝난 뒤 실행한다. 실제 장치 및 modal 검증은 이 명령으로 대체되지 않는다.

```sh
cmake --build build/release --target seam_conversion_review_tests seam_u2_tests seam_interchange_service_tests seam_ustx_interchange_tests seam_smf_interchange_tests seam_editor_native seam_voicebank_studio_native -j4
ctest --test-dir build/release --output-on-failure -R '^seam_(conversion_review|u2|interchange_service|ustx_interchange|smf_interchange)_tests$'
cmake --build build/debug --target seam_conversion_review_tests seam_u2_tests seam_interchange_service_tests seam_ustx_interchange_tests seam_smf_interchange_tests -j4
ctest --test-dir build/debug --output-on-failure -R '^seam_(conversion_review|u2|interchange_service|ustx_interchange|smf_interchange)_tests$'
python3 -B scripts/verify_tracked_source_closure.py --root .
```

마지막 소스 완전성 검사는 통합 담당자가 검토한 신규 소스를 정상적으로 등록한 뒤 통과시킨다. 검사 자체를 완화하지 않는다. 녹음 실패 주입 테스트는 입력 장치 경계를 분리하면서 적절한 기존 테스트 대상에 추가하고, 그 대상도 이 목록에 포함한다.

### 묶음 2 — 원본 여성 가수 제작부터 한 곡 완성까지

대상: 기존 P3, U9–U22/U25/U42의 연결된 제작 경로.

주요 소유 영역은 `seam-voice-design`, `seam-voicebank-production`, native Studio/Designer, 기존 original-singer workflow 및 song journey 테스트다. 새 데모 엔진을 만들지 않고 이 경로를 확장한다.

1. 빈 작업 공간에서 목소리 생성 → 발성/공명/자음/스타일 편집 → 저장·재열기 → 미리듣기.
2. 생성 또는 실제 입력 → take 편집/재생성·재녹음 → 기존 수동 작업 보존 → 검사·검토 → 개발 후보 패키지 → 설치.
3. 편집기에서 설치한 가수 선택 → 처음 보는 가사로 30–60초 곡 → pitch/phoneme timing/vibrato/timbre 수정 → 저장·재열기 → 최종 WAV 출력.
4. 취소·오래된 비동기 결과·실패 복구·원본/설치 리소스 불변성을 같은 흐름에서 확인한다.
5. 실제 네이티브 UI에서 JSON 수작업 없이 수행하고, fixture 기반 서비스 테스트와 별도 기록한다.

**완료 산출물:** 생성 경로와 실제 입력 경로 각각의 제작 프로젝트, 레시피/뱅크/곡/오디오 식별 정보, native 작업 기록, 재현 가능한 자동 회귀 테스트.

**승인 경계:** 이것은 macOS 제작 흐름의 공학적 완료다. 개발용 리소스에 가짜 청취 승인이나 출시 권한을 부여하지 않는다. 가수 품질과 전체 Beta GO는 다음 묶음에서 판정한다.

### 묶음 3 — 실제 가수·언어·표현을 제품 수준으로 완성

대상: 기존 P4–P6, U6–U8/U15–U20/U26–U28/U35–U43.

- **절차적/클래식 가수:** 실제 음역·음소·스타일·렌더러·지원 표현 표를 확정하고, 자음·전이·정체성·노래 안에서의 표현 효용을 개선한다.
- **다국어:** 영어 사전/발음 자원과 한국어 사전 의존 예외를 완성하고, 세 언어의 실제 sung phrase를 승인한다. phonemizer 코드 존재만으로 언어 지원 완료 처리하지 않는다.
- **신경망:** 기존 단계 비교 자료로 source/vocoder/acoustic 결함을 좁힌다. 가설·입력·지표·부작용·중단 조건을 먼저 고정하고 한 번에 한 수리 계열만 평가한다. 실패한 실험을 근거 없이 반복하지 않는다.
- **표현/창작:** 필수 자동 표현, alternate takes·화음, 비교·재생성·수동 편집 보존을 실제 곡과 native UI에서 끝낸다. unsupported 상태는 정직한 실패 처리일 수 있지만 필수 기능의 최종 구현을 대신하지 못한다.
- **리소스:** 실제 뱅크·레시피·모델·캐릭터의 hash, 버전, 출처·권한, 품질과 실행 예산을 검증하여 빈 출시 리소스 표를 채운다.
- **검증:** 원래 계약의 언어별 최소 60개 phrase, 3곡, 독립 창작자 최소 5명 기준을 유지한다. 독립 청취와 무도움 제작 관찰은 자동 지표와 따로 판정한다.

**완료 산출물:** 확정된 리소스/기능 표와 평가 프로필, 실제 후보 파일, 학습 외 평가, 실패/개선 비교, 독립 음악·창작 평가 기록.

**핵심 제한:** 자동 테스트·ASR·스펙트럼 수치만으로 자연스러운 여성 가수나 창작 효용을 승인할 수 없다. 해당 평가를 기다리는 동안 입력·편집·배포 등 독립적인 개발은 계속한다. 절차적 가수 성공으로 필수 신경망 가수 요구사항을 삭제하지 않는다.

### 묶음 4 — 설치 제품과 전체 Beta GO 닫기

대상: 기존 P7, U33–U34/U44–U48.

1. macOS의 실제 후보를 고정하고 standalone/CLAP/VST3/AUv2 설치·호스트 작업·복구·지원 자료를 검증한다.
2. 실제 후보에 대해 서명·공증·설치/제거·재설치·장시간 실행·여러 인스턴스·크래시 복구·내보내기를 확인한다.
3. 증거 묶음을 복원한 환경에서도 같은 후보·리소스·결과를 감사하고, 전체 제품 gate가 빠진 증거를 거부하는지 검증한다.
4. Windows는 현행 README의 TODO를 유지한다. 환경 확보 뒤 필수 플랫폼/호스트 검증을 실행하고, 모든 원래 조건을 만족할 때만 전체 Beta GO를 승인한다.

**완료 산출물:** 정확한 설치 후보와 플랫폼/호스트별 실행 증거, 복원 감사, 미해결 필수 항목 없는 최종 판정.

**일정과 범위 구분:** Windows 없이 macOS 개발은 계속할 수 있다. 그러나 [현행 계약과 README](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/README.md:26) 기준으로 Windows를 미룬 상태를 전체 Beta GO라고 부를 수는 없다.

## 7. 병렬 작업 방식과 완료 판정

동시에 세 트랙까지만 운영한다. 같은 공유 파일을 여러 에이전트가 동시에 수정하지 않고, 최종 빌드·통합·검증은 한 곳에서 직렬로 진행한다.

| 트랙 | 책임 | 다음 결과 |
|---|---|---|
| A: native 제작 경험 | modal/문서 lifecycle/Studio/Designer/편집기 | 묶음 1의 UI와 묶음 2의 실제 완주 |
| B: 음악·가수 엔진 | MIDI 시간/손실 정책, 발음·표현, bounded acoustic/neural 수리 | 음악 의미 보존과 실제 가수 능력 |
| C: 독립 검증·통합 | 원래 조건 추적, 반례, 리소스 식별, 실제 실행/리뷰 | 각 묶음의 승인 가능한 증거와 통합 |

각 큰 묶음은 다음 순서로 닫는다.

1. 사용자 행동과 종료 조건을 고정한다.
2. 실패 재현과 양성 대조군을 확보한다.
3. 필요한 구현과 연결을 끝낸다.
4. 영향 범위의 strict build/test 및 실제 사용 경로를 검증한다.
5. 지정된 리뷰 작업 `대기`에서 독립 검토를 받고 지적을 수정한다.
6. scoped commit/push와 원격 확인 후 원래 U 단위의 종료 조건 충족 여부를 갱신한다.

이 문서는 새로운 독립 승인을 받은 것이 아니다. 이번에는 기존 P1 승인 기록을 확인했고, 두 read-only 에이전트의 코드 감사를 교차 확인했다.

진행 표에는 단순한 “진행 중” 대신 **소스 구현 / 자동 검사 / native 실행 / 음악·사용자 평가 / 출시 승인**을 나눠 기록한다. 이 단계들을 임의 가중치로 합산한 새 퍼센트는 만들지 않는다.

확인 시 디스크 여유는 약 5.6 GiB였다. 전체 중복 빌드나 대규모 학습을 시작하기 전 필요한 공간을 점검하되, 이번 분석에서는 기존 자료를 삭제하지 않았다.

## 8. 다음 개발에서 바로 할 일

**지금은 묶음 1을 끝내고, 곧바로 묶음 2의 실제 제작 흐름을 완주시키는 것이 최우선이다.** 음향 연구는 묶음 3의 제한된 병렬 트랙으로 진행한다.

이 순서라면 다음 보고의 중심이 “내부 수리 몇 개를 더 했다”가 아니라 “가져온 곡의 길이가 보존되고, 실제 입력 실패가 정직하게 처리되며, 사용자가 직접 만든 가수로 한 곡을 끝냈다”가 된다. 그 다음에 남은 품질·다국어·신경망·배포 조건을 원래 계약에 맞춰 닫는다.

현재 근거만으로 완료 날짜나 24시간 연속 작업 소요 시간을 정확히 계산할 수는 없다. 특히 가수 품질과 독립 평가·플랫폼 환경은 코드 작성 속도만으로 해결되지 않는다. 먼저 이 두 제작 묶음을 닫고 실제 소요와 잔여 결함을 바탕으로 일정을 갱신하는 것이 타당하다.
