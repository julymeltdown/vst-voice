# SEAM 진행도 점검 및 완료 실행 계획

작성일: 2026-09-22

확인 기준 커밋: `8820bc4343ba735fab1bb0a9abb772103d15021e`

문서 성격: 현재 상태 분석과 향후 실행 계획. 아래 작업을 이미 구현하거나 승인했다는 뜻이 아니다. 기존 R1–R20 / U1–U48의 전체 목표를 축소하거나 대체하지 않는다.

## 1. 요약 결론

SEAM은 상당한 음성 합성·편집·뱅크 제작 기반을 갖췄지만, 마지막 몇 가지 수정만으로 출시할 단계는 아니다. 핵심 미완료 항목은 실제로 사용할 만한 가수의 품질, 네이티브 앱에서 완결되는 제작 과정, 그리고 정확한 출시 후보에 대한 검증이다.

다음 큰 목표는 **원본 여성 가수 하나를 설계하고, 편집 가능한 뱅크로 설치한 뒤, 처음 보는 가사로 30–60초 곡을 편집·저장·내보내는 것**이다. 이미 있는 약 39.5초 곡 테스트와 제작 서비스를 확장한다. 별도의 고립된 데모를 새로 만드는 것을 기본 경로로 삼지 않는다.

이 목표의 공학적 성공과 음악적 성공은 별도로 판정한다. 기능이 작동해도 발음·음색·음악적 품질이 합격하지 않으면 가수의 품질 검증은 끝난 것이 아니다.

## 2. 현재 진행도와 집계 기준

| 지표 | 현재 값 | 해석 |
|---|---|---|
| 로컬 구현 단위 승인 | 6/48, **12.5%** | U1–U5와 로컬 POSIX 범위 U29. 전체 플랫폼 승인율이 아니다. |
| 미승인 구현 단위 | 42/48 | 상당수가 부분 구현돼 있다. 42개를 처음부터 개발해야 한다는 의미가 아니다. |
| 전체 제품 요구사항의 출시 승인 | 0/20, **0%** | 동일 출시 후보에 대한 전체 Beta 승인 증거가 아직 없다. 코드 완성도가 0%라는 의미가 아니다. |
| 실제 코드 구현량·남은 노력의 비율 | 산출하지 않음 | 단위 크기와 부분 구현 수준이 다르므로 승인율로 환산할 수 없다. |
| 계약에 등록된 출시 리소스 | 0개 | 개발용 음원·뱅크·모델이 없다는 뜻이 아니라, 출시 리소스 등록과 검증이 미완료라는 뜻이다. |

과거 50–60% 등의 공학적 추정치는 최신 단위별 근거가 없어 다시 사용하지 않는다. 최근 포먼트/내보내기 개선도 부분 구현 승인이지 U39 전체 완료가 아니므로 단위 수를 올리지 않는다.

근거:

- [U1–U5 구현 승인 기록](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/FULL_SCOPE_BETA_EXECUTION.md:1322)
- [U29 승인 범위 및 이전 집계 정정](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/INTEGRATED_SINGER_EXECUTION.md:383)
- [출시 리소스 미등록 상태](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/product/full-product-beta-contract.json:49)

### 이번 확인 범위

- 로컬 HEAD와 실제 원격 `master`는 모두 위 기준 커밋이다.
- 문서 작성 전 미커밋 변경은 `tests/test_voice_design.cpp`의 32줄 회귀 테스트 하나뿐이었다. 변경은 보존했다.
- 계약의 요구사항 20개, 하위 사례 83개, 리소스 0개를 직접 확인했다.
- 평가 프로필 검사에서는 정의 오류가 없지만, 기준·예산·기준 장비·프로필 확정·리소스 표 등 13개 미확정 항목이 반환됐다.
- 이전 검토에서 실행한 계약/리더/게이트/보고서 테스트 44개는 통과했다. 이번 새로고침에서 다시 실행한 결과로 표기하지 않는다.
- 보존된 레시피 테스트 로그는 39개 통과, 새 회귀 테스트 1개 실패다. 수정은 아직 없다.
- 전체 C++ 빌드, 전체 테스트, 실제 앱 조작, 청취 및 설치 검증은 이번 문서 작성에서 새로 수행하지 않았다.
- 이 문서 외에 구현을 변경하거나 커밋·푸시하지 않는다.

## 3. 이미 구현된 것과 남은 차이

| 영역 | 확인된 구현 | 아직 완료로 볼 수 없는 이유 |
|---|---|---|
| Voice Designer | 발성·공명·여러 자음 계열, 시드, 저장/재열기, 미리듣기와 비교 | 원하는 원본 여성 가수의 발음·정체성·음악적 품질이 승인되지 않음 |
| 곡 제작 | 약 39.5초 일본어 곡, 표현 수정, 실행 취소, 저장/재열기, 실제 오디오 내보내기 테스트 | 결과가 명시적으로 청취 미검토·창작자 사용 미관찰·출시 불가 상태 |
| 표현/렌더링 | 공유 성능 컴파일러, 여러 표현 채널, Spectral Classic 독립 포먼트, 불완전 최종 출력 거부 | 모든 필수 기능의 리소스/렌더러 조합과 음악적 결과를 아직 승인하지 않음 |
| 가져오기/내보내기 | USTX/SMF 코덱, 제한된 파일 읽기, 변환 서비스 | 네이티브 가져오기 검토 콜백이 연결되지 않음 |
| 언어 | 일본어 읽기 처리, 영어·한국어 음소 및 문맥 처리 | 영어는 26개 초기 사전, 한국어는 사전 의존 예외 미검증. 실제 가수와 연결된 언어별 품질 승인 필요 |
| 신경망 | 학습·추론·모델 배포 기반과 실제 음향 실험 | 무성 자음 문제와 미검증 음질, 실제 출시 모델의 적격성 미완료 |
| 제품 검증 | 증거 검증기, 후보 식별·복구·감사 기반 | 실제 합격 리소스, 확정된 평가 기준, 설치/호스트/독립 사용자 증거 부족 |

곡 테스트는 [결과 상태를 명시적으로 미검토로 기록한다](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_original_singer_song_journey.cpp:890). 검증기 구현이 있다는 사실과 실제 제품 증거가 합격했다는 사실도 구분해야 한다.

## 4. 즉시 실행할 첫 세 작업 묶음

### P1. 레시피 기능 검사와 저장 경계 정리

목적: 이미 재현된 결함을 해결하고, 이후 음성 제작 작업이 의존하는 레시피 경계를 안정화한다.

주요 코드:

- [recipe_resource.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voice-design/src/recipe_resource.cpp:42)
- [test_voice_design.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_voice_design.cpp:1651)
- [voice_designer_session.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voice_designer_session.cpp)
- [test_voice_designer_workflow.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_voice_designer_workflow.cpp)

개발 내용:

1. 스키마 지원 여부와 실제 소스 기능의 허용 여부를 분리한다.
2. 디코딩된 레시피 내용으로 유성 마찰음·유성 폐쇄음 등의 요구 기능을 검사한다. 새 스키마에 오래된 기능이 들어 있어도 같은 제한을 적용한다.
3. 복합 자음과 기반 자음을 참조하는 바인딩의 요구 기능을 명시하고, 디코더·조음 계획·스트림 진입점에서 같은 판정을 검증한다.
4. 기본 허용 설정, 기존 직렬화 바이트·해시·버전·시드가 불필요하게 달라지지 않도록 한다.
5. 저장/재열기·실행 취소·외부 파일 변경 충돌·기존 설치 가수 불변성을 유지한다.

종료 조건:

- 현재 실패 테스트가 실제 수정으로 통과한다. 검사를 삭제하거나 허용 조건을 완화해 통과시키지 않는다.
- 모든 지원 스키마와 독립 허용 플래그 조합을 검증한다.
- Debug/Release 음성 설계 및 Designer 테스트가 통과하고 독립 리뷰가 승인한다.
- 이 수정만으로 U18 전체 또는 가수 품질이 완료됐다고 집계하지 않는다. U18의 원래 조건과 U9 의존성은 별도로 확인한다.

### P2. 네이티브 USTX/MIDI 가져오기 완성

목적: 이미 존재하는 코덱을 사용자가 실제 앱에서 사용할 수 있도록 연결한다. P1과 독립적으로 시작할 수 있다.

주요 코드:

- [application_controller.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-standalone/src/application_controller.cpp:828)
- [native_editor_app.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-standalone/src/native_editor_app.cpp:363)
- [interchange_service.cpp](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/src/interchange_service.cpp)
- [seam-interchange](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-interchange)

개발 내용:

1. `reviewInterchangeImport`를 구현하고 실제 네이티브 앱 구성에 연결한다.
2. 변환 손실·경고를 누락 없이 탐색 가능한 검토 화면으로 제공한다. 가수가 없는 상태와 자원 재연결 필요성을 표시한다.
3. 검토 중 문서가 바뀌면 오래된 결과를 적용하지 않는다. 취소 시 현재 문서와 원본 파일을 유지한다.
4. 수락한 결과는 저장되지 않은 새 문서로 열고, 기존 변경의 저장/폐기/취소 처리와 통합한다.
5. 임베디드 편집기에서도 공통 검토 모델을 재사용하되, 어댑터별 동작을 별도로 검증한다.
6. 고정된 OpenUtau 버전과 실제 DAW를 통한 교환 검증을 수행한다. 로컬 코덱 왕복 통과로 외부 호환성을 대체하지 않는다.

종료 조건:

- 앱 메뉴에서 USTX/MIDI 가져오기 → 손실 검토 → 취소 또는 적용이 작동한다.
- 오래된 검토 결과, 저장 취소, 원본 보존, 출력 충돌의 회귀 테스트가 있다.
- 기존 코덱/서비스 테스트가 유지되고 실제 AppKit 조작 증거가 있다.
- 네이티브 연결 완료와 외부 프로그램 교환 완료를 따로 기록한다. 실제 DAW 교환 미실행 상태에서 U31 전체를 승인하지 않는다.

### P3. 원본 가수 제작부터 곡 내보내기까지 연결

목적: 다음 큰 사용자 가치 단위를 완성한다. 검증 시나리오 정리는 즉시 시작할 수 있지만 레시피 의존 부분의 최종 검증은 P1 이후 수행한다.

주요 코드:

- [네이티브 Voicebank Studio](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp)
- [뱅크 제작 모듈](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production)
- [기존 원본 가수 곡 테스트](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_original_singer_song_journey.cpp)
- [최종 렌더링 완전성 테스트](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_final_render_completeness.cpp)

개발 내용:

1. 빈 작업 공간에서 레시피 생성 → 저장/재열기 → 미리듣기 → 생성 작업을 진행한다.
2. 생성한 소스를 편집·재녹음/재생성하고, 기존 수동 편집을 보존한다. 실제 입력을 통한 제작 경로도 계속 유지한다.
3. 소스 검토 → 후보 패키지 → 설치 → 곡에서 가수 선택의 누락된 연결을 마무리한다.
4. 기존 39.5초 곡과 별도의 처음 보는 가사로 표현 수정·저장·재열기·최종 출력까지 검증한다.
5. 취소·오래된 비동기 결과·복구·좁은 창·긴 텍스트·키보드 접근성을 같은 흐름에서 확인한다.
6. 검토를 통과하지 않은 개발 가수는 개발 정책으로만 설치한다. UI를 시험하려고 가짜 승인 또는 출시 권한을 만들지 않는다.

종료 조건:

- JSON 수작업 없이 실제 네이티브 UI로 제작 흐름을 완주한다.
- 곡·레시피·뱅크·오디오의 정확한 식별 정보와 변경 이력이 보존된다.
- 표현 변경이 실제 오디오에 반영되고, 요청한 보컬 일부가 빠진 결과는 최종 성공으로 처리되지 않는다.
- 공학적 흐름은 별도로 승인한다. 발음·여성 가수 정체성·음악적 품질은 P4/P6 평가 없이 합격으로 처리하지 않는다.

## 5. 병렬로 진행할 음향 및 전체 기능 마무리

### P4. 첫 가수와 신경망 가수의 음악적 품질 확보

관련 단위: U6–U8, U15–U20, U35–U39 및 U42/U43의 품질 조건.

현재 46개 비교 음원의 청취 자료를 이용해 원음 → 보코더 재구성 → 음향 모델과 보코더 출력의 결함을 비교한다. 이 비교가 단일 원인을 자동으로 증명하는 것은 아니다. 다음 수정은 비교로 좁혀진 한 가설, 고정된 데이터·지표·부작용 기준·중단 조건을 가져야 한다.

실제 작업은 다음으로 나눈다.

- 절차적 가수: 자음 명료도, 전이, 음역과 스타일, 원하는 정체성, 표현 제어가 실제 곡에서 유효한지 검증하고 개선한다.
- 신경망 가수: 무성 자음의 약한 에너지와 부적절한 주기성을 단계별로 조사한다. 실패한 보정 계열을 근거 없이 재학습하지 않는다.
- 배포: 선택한 실제 모델과 보코더를 네이티브 실행·미리듣기·내보내기까지 연결하고 지연·메모리·취소를 측정한다.
- 대안: 현재 방식이 고정된 목표를 충족하지 못하면 호환 가능한 다른 방식의 적합성을 평가한다. 사용 권한과 리소스 적격성은 별도로 확인하며, 다운받거나 연결했다는 사실만으로 완료 처리하지 않는다.

종료 조건은 학습 손실 감소가 아니라 **최종 오디오, 학습에 사용하지 않은 자료, 독립 평가, 실제 배포 경로**의 합격이다. 절차적 방식의 성공으로 필수 신경망 가수 요구사항을 면제하지 않는다.

현재 연구 상태: [단계별 청취 자료 및 중단된 실험](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/listening/2026-09-22-stage-localization/decision.md)

### P5. 다국어·표현·창작 도구 완성

관련 단위: U23–U28, U30–U32, U38–U41 및 공유 렌더링 조건.

- 영어 초기 사전을 적법하고 버전이 고정된 실제 어휘 자원으로 확장하고, 추정 발음과 검토된 발음을 계속 구분한다.
- 한국어 어휘·문맥 예외와 일본어 읽기/발음 수정 경로를 실제 가수의 음소 목록과 연결한다.
- 표현마다 지원하는 리소스/렌더러 조합을 명시한다. 컴파일러에 채널이 있다는 사실만으로 실제 합성 지원을 선언하지 않는다.
- 자동 제안 수락과 수동 잠금, 부분 재생성, 테이크/화음이 서로의 편집을 훼손하지 않게 한다.
- 캐릭터는 선택 가수의 정체성과 재생 상태를 설명하는 역할로 연결하고, 모션 축소와 편집 공간·텍스트 가독성을 유지한다.

종료 조건: 지원한다고 광고하는 조합에서 처음 보는 가사의 발음과 표현이 실제 오디오·저장/재열기·최종 출력까지 일관되게 유지된다. 언어별 독립 품질 평가는 P6에서 완료한다.

### P6. 출시 리소스와 독립 사용자 평가

관련 단위: U42–U43.

- 실제 녹음 입력, 생성 원본, 파생 소스, 레시피, 모델/보코더, 사전, 스타일 및 캐릭터의 식별 정보·출처·권리·검토 상태를 확정한다.
- 언어/음역/스타일/기능 지원표와 평가 기준은 최종 평가 전에 확정한다. 실패 후 기준을 낮춰 합격시키지 않는다.
- 기존 계약의 언어별 최소 60구절, 완성곡 최소 3곡, 독립 창작자 최소 5명과 언어·플랫폼 범위 조건을 수행한다.
- 평가된 리소스가 바뀌면 영향받는 증거를 무효화하고 다시 검증한다.

종료 조건: 실제 출시 리소스가 확정된 표에 등록되고, 독립적인 발음·음색·음악성·창작 과정 평가가 같은 리소스에 연결된다.

근거: [기존 U42/U43 조건](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:929)

### P7. 설치 제품·DAW·복구·최종 Beta 승인

관련 단위: U33–U34, U44–U48.

1. macOS 독립 실행과 REAPER/Bitwig의 CLAP·VST3, Logic의 AUv2를 실제 설치 후보로 검증한다.
2. 재시작 직후 오프라인 출력, 템포/샘플레이트 변경, 탐색/반복 재생, 리소스 누락, 오래된 렌더 결과, 실시간 표현을 확인한다.
3. 실제 충돌·복구·지원 자료 생성, 개인정보 제외, 동의·취소·삭제 동작을 검증한다.
4. 서명·공증·설치/재설치/제거와 정확한 후보/리소스 해시가 연결된 증거를 확보한다.
5. Windows 환경이 준비되면 해당 네이티브 동작·설치·DAW 항목을 재개한다.
6. U45/U46의 기존 검증 코드를 실제 후보 증거와 연결하고, 복원한 증거 아카이브로 전체 승인을 수행한다.

macOS 결과를 먼저 완성한다. Windows TODO는 macOS 개발을 막지 않지만, 현재 전체 Beta 계약에서는 면제되지 않는다. READY와 이후 외부 코호트 CLOSED도 서로 다른 상태로 유지한다.

근거: [현재 플랫폼 정책](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/README.md:26), [실제 의미 검증기 연결](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/external_beta/release_gate.py:136)

## 6. 병렬 실행 및 통합 규칙

동시에 세 개의 구현 트랙을 운영한다.

| 트랙 | 초기 담당 | 다음 담당 |
|---|---|---|
| A: Designer/제작 | P1 레시피 결함 | P3 생성·편집·검토·설치 연결 |
| B: 편집기/교환/호스트 | P2 네이티브 가져오기 | P5 창작 흐름과 P7 macOS 호스트 |
| C: 음향/모델/리소스 | P4 가수 품질과 결함 위치 조사 | P6 리소스 확정과 독립 품질 검증 |

통합 담당자는 CMake, 공용 직렬화·파일 I/O, 핵심 native controller 등 충돌 위험 파일의 변경을 조정한다. 독립된 C++ 빌드가 같은 빌드 디렉터리를 동시에 변경하지 않게 한다. 학습과 큰 빌드를 무제한 동시 실행하지 않는다.

의존 순서:

- P1과 P2는 즉시 병렬 진행 가능하다.
- P3의 시나리오와 연결 정리는 즉시 가능하지만 레시피 관련 최종 검증은 P1 이후 진행한다.
- P4는 병렬 진행하며, 청취 대기 때문에 P1/P2/P3 전체를 중단하지 않는다.
- P5의 언어 자원과 UI는 병렬 개발할 수 있지만, 최종 가창 품질 승인은 실제 가수 자원과 합쳐서 받는다.
- P6는 P3/P4/P5의 적격 후보를 통합한다.
- P7의 테스트 도구와 코드 수정은 먼저 할 수 있지만, 최종 설치·호스트 증거는 후보와 리소스 고정 후 확보한다.

각 큰 사용자 흐름 완료 시 기존 리뷰 작업 [대기](codex://threads/01a0a066-1eba-71f2-8c0d-e21f9419cbcc)에 기준 커밋, 변경 범위, 재현 명령, 실패/성공 증거, 남은 조건을 전달한다. 리뷰 수정 반영 후 다시 확인한다. 리뷰 승인, 실제 런타임 검증, 제품 품질 승인은 서로 대체하지 않는다.

## 7. 초기 검증 명령

아래 명령은 향후 구현 후 실행할 절차이며 이번 문서 작성에서 실행한 결과가 아니다. 작업 디렉터리는 `/Users/lhs/Downloads/project-seam-usable-alpha-u3-master`이다. 기존 빌드 구성과 선택 기능을 확인하고, 타깃이 없으면 원인을 확인한 후 해당 구성을 재생성한다. 빠진 타깃을 성공으로 간주하지 않는다.

P1:

```sh
cmake --build build/release --target seam_voice_design_tests seam_voice_designer_tests -j4
ctest --test-dir build/release -R '^seam_(voice_design|voice_designer)_tests$' --output-on-failure
cmake --build build/debug --target seam_voice_design_tests seam_voice_designer_tests -j4
ctest --test-dir build/debug -R '^seam_(voice_design|voice_designer)_tests$' --output-on-failure
```

P2의 기존 서비스/코덱 회귀 검사:

```sh
cmake --build build/release --target seam_interchange_service_tests seam_ustx_interchange_tests seam_smf_interchange_tests -j4
ctest --test-dir build/release -R '^seam_(interchange_service|ustx_interchange|smf_interchange)_tests$' --output-on-failure
```

P2에는 이 명령만으로 충분하지 않다. 새 controller/UI 회귀 검사를 등록하고 Debug에서도 실행하며, 실제 AppKit 및 외부 프로그램 교환 결과를 별도로 확보해야 한다.

P3의 기존 곡/표현/내보내기 회귀 검사:

```sh
cmake --build build/release --target seam_original_singer_song_journey_tests seam_formant_expression_tests seam_final_render_completeness_tests -j4
ctest --test-dir build/release -R '^seam_(original_singer_song_journey|formant_expression|final_render_completeness)_tests$' --output-on-failure
```

공통 통합 검사:

```sh
git diff --check
python3 -B scripts/verify_tracked_source_closure.py --root .
python3 -B -m unittest tests.external_beta.test_full_product_definition tests.external_beta.test_full_product_reader tests.external_beta.test_full_product_gate tests.external_beta.test_full_product_report
```

테스트 하나 수정할 때마다 전체 고비용 검증을 반복하지 않는다. 영향 범위 테스트 → 작업 묶음별 Debug/Release 및 실제 흐름 → 후보 고정 후 전체 회귀/설치 검증 순서로 진행한다. 전체 회귀는 필요한 타깃을 새로 빌드한 후 수행하며, 오래된 바이너리의 통과를 새 코드의 통과로 간주하지 않는다.

## 8. 자동화할 수 있는 것과 별도 증거가 필요한 것

| 항목 | Codex/자동화 역할 | 대체할 수 없는 조건 |
|---|---|---|
| 코드·상태 보존 | 회귀 테스트, 오류 주입, 직렬화/해시 비교, 취소·복구 검증 | 실제 플랫폼 동작을 실행하지 않고 추정할 수 없음 |
| 음향 | 음정·무음·클리핑·구간별 에너지·재현성·성능 측정, 비교 자료 작성 | 자동 지표만으로 명료도·가수 정체성·음악성을 확정할 수 없음 |
| UI | 실제 앱 조작, 화면·접근성 확인, 작업 흐름 회귀 검사 | 독립 창작자의 도움 없는 사용을 에이전트 실행으로 대체할 수 없음 |
| 호환성 | OpenUtau/DAW 교환 실행과 차이 검출, 설치 자동화 | 실행하지 않은 호스트·Windows 결과를 만들 수 없음 |
| 출시 | 후보 식별·증거 수집·복원 감사 | 권리·서명 자격·독립 검토 사실을 코드로 생성할 수 없음 |

외부 증거가 필요한 항목은 그 항목만 대기시킨다. 다른 구현 가능 작업까지 일괄 중단하지 않는다. 다만 대기 항목이 필수인 상태에서 전체 Beta GO를 선언하지도 않는다.

## 9. 앞으로의 진행 보고 방식

각 보고는 다음 다섯 가지를 고정해서 표시한다.

1. 기준 커밋 및 미커밋 변경.
2. 실제 완료한 사용자 작업 흐름.
3. 승인 단위 수와 승인 범위. 부분 구현을 한 단위로 더하지 않음.
4. 이번에 실제 실행한 검증과 재사용한 과거 증거의 구분.
5. 남은 가장 중요한 세 항목과 다음 작업 묶음.

U1–U48 각각의 기존 조건을 현재 코드·테스트·남은 조건·승인 커밋에 연결해 한 곳에서 유지한다. 단순 문서 정리만으로 진척을 올리지는 않는다. 새 일정은 첫 작업 묶음들의 실측 소요와 음향 연구 상태를 바탕으로 갱신하며, 승인율 12.5%를 이용해 남은 시간을 기계적으로 계산하지 않는다.

최종 판단: **방향은 목표와 맞지만, 다음 진척의 단위는 개별 기능 추가가 아니라 실제 가수와 곡 제작 과정의 완결이어야 한다.** 당장 P1/P2를 병렬로 닫고 P3의 네이티브 제작 흐름을 완성하는 동안, P4에서 가장 불확실한 음악적 품질을 별도 관리하는 것이 현실적인 완료 경로다.
