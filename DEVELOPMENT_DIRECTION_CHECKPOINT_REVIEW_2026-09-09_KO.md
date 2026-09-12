# SEAM 개발 방향 심층 리뷰 — 최신 구현 체크포인트

## 1. 결론: 구조는 유지하고, 개발의 중심을 실제 가수와 완성된 제작 경험으로 옮겨야 한다

**개발 방향은 조건부로 적절하다. 지금까지의 코드를 버리거나 처음부터 다시 만들 이유는 없다. 그러나 현재 상태를 ‘Beta GO 95%, 마무리만 남음’으로 설명하는 것은 부정확하다.** 안전한 편집·저장·생성·검토·설치·렌더 연결은 실질적으로 진전됐지만, 실제 여성 가수의 일반 가사 발음, 음질, 고급 표현, neural 합성, 전체 호스트 동작 및 독립적인 제품 수락은 아직 상당히 남아 있다.

이번 검토의 판정은 다음과 같다.

- **아키텍처: 유지.** 악보/발음/연주 의도와 렌더러를 분리하고, immutable take·버전·소유권·검토를 명시적으로 관리하는 선택은 전체 목표에 부합한다.
- **실행 우선순위: 재조정.** 추가 계약·상태 필드·좁은 fixture 테스트만 늘리는 방식에서, 실제 가수 하나로 생성부터 새 노래 출력까지 완주하는 방식으로 이동해야 한다.
- **통합 상태: 미수락.** 이번 전체 Release 빌드는 최신 Studio 테스트의 컴파일 오류로 실패했다. 별도 재빌드한 집중 테스트에서도 Phase12B 실패가 남았다.
- **Full-Scope Beta GO: NO-GO.** 이것은 개발 중단 지시가 아니라 출시 판정이다. 코드를 계속 발전시킬 수 있으며, 어떤 내부 작업을 먼저 해야 하는지도 확인됐다.

사용자가 요구한 목표는 단순한 샘플러나 모음 신디사이저가 아니다. **녹음 없이도 원본 여성 목소리를 만들고, 실제 녹음/생성 재료를 편집해 bank로 생산하며, 일본어·영어·한국어의 새 곡을 classical/neural 경로와 필요한 표현·호스트에서 완성하는 제품**이다. 이번 권고는 그 범위를 줄이지 않는다.

## 2. 평가 기준: 테스트 통과율과 제품 완성도를 섞지 않는다

기준일은 2026년 9월 9일이며, 현재 로컬 작업본을 검사했다. 승인된 Full-Scope 계획은 R1–R20, V01–V18 및 48개 구현 단위 전체를 요구한다. 중간 pilot이나 classical 경로의 성공은 개발 체크포인트이며 대체 Beta GO가 아니다. [승인된 완료 기준](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:23)

이 보고서는 증거를 네 종류로 구분한다.

1. **소스 구현 확인:** 실제 호출부와 소비 경로가 존재한다. 컴파일·동작·음질까지 보증하지 않는다.
2. **현재 engineering 실행:** 이번 소스로 재빌드한 타깃과 실제 실행한 테스트가 입증한 제한된 동작이다.
3. **제품 자격:** 실제 리소스, 음역·발음·스타일·청취·창작 작업이 정해진 기준을 충족한다.
4. **출시 자격:** 동일한 서명·설치 후보가 필요한 플랫폼·호스트·복원 감사에서 수락됐다.

현재 실행 ledger가 수락했다고 기록한 단위는 U1–U5, 즉 **5/48 = 10.4%**다. 이것은 과거 단위 수락 기록의 비율이지 이번에 전부 재수락한 결과도, 전체 코드 구현량이나 남은 시간의 비율도 아니다. U6 이후에도 상당한 코드가 있으므로 ‘나머지 89.6%는 아무것도 없다’는 해석 역시 틀리다. [ledger의 명시적 상태](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/FULL_SCOPE_BETA_EXECUTION.md:544)

전체 제품 완성률은 이번 보고서에서 새 숫자로 추정하지 않는다. 실제 음향·리소스·플랫폼별 수락 자료가 없는 상태에서 60%나 80%를 붙이면 근거보다 정밀한 숫자가 된다. 앞으로는 **수락 단위 수, 완주 가능한 사용자 작업, 실제 qualified 리소스, 현재 회귀 상태**를 별도로 보고하는 편이 정확하다.

## 3. 현재 실행 결과: 기반은 동작하지만 통합 완료 상태는 아니다

### 3.1 전체 빌드는 실패했다

`cmake --build build/release -j 4`를 실행했고 실패를 재현했다. 최신 `test_studio_manifest_draft.cpp`가 controller의 private 메서드 `selectedAudioPath()`를 세 곳에서 호출한다. 같은 파일이 core와 별도 Studio draft 테스트 타깃에 포함되므로 둘 다 빌드를 막는다. [실패 지점](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_studio_manifest_draft.cpp:293), [private 선언](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/include/seam/native_ui/voicebank_studio.hpp:324)

이것은 현재 확인한 테스트 통합 실수이지, 그 자체로 음성 엔진 전체가 고장 났다는 뜻은 아니다. 반대로 앱 일부가 빌드된다는 이유로 전체 빌드 PASS라고 할 수도 없다. 테스트에서는 공개된 manifest 경로와 selected unit의 상대 audio 경로를 조합하면 된다. 테스트를 편하게 만들기 위해 production 내부 API를 public으로 넓힐 필요는 없다.

**최신 Studio draft 16개 테스트는 현재 PASS로 집계할 수 없다.** 이전 10개 테스트의 성공이나 새 테스트 작성 완료를 최신 16개 실행 성공으로 바꾸어 기록해서는 안 된다.

### 3.2 따로 재빌드한 집중 검증은 25개 중 23개 통과했다

전체 빌드 실패 후, 해당 테스트 파일을 필요로 하지 않는 앱·플러그인·집중 테스트 타깃을 명시적으로 재빌드했다. 이 제한된 빌드는 성공했다. 이어 선택한 CTest 25개를 직렬 실행한 결과는 **23 PASS / 2 FAIL, 28.18초**였다. 등록된 전체 CTest는 120개지만, **이번 결과는 120개 전체 실행 결과가 아니다.**

남은 실패는 다음과 같다.

- `seam_phase12b_tests`: 직접 재실행해도 exit 40. 현재 테스트의 positive-path 준비 조건에서 종료되므로 뒤쪽 Final lifecycle 검증까지 통과했다고 볼 수 없다. 상세 원인은 아래 호스트 절에서 구분한다.
- `seam_tracked_source_closure`: 실행 시점의 필수 입력 335개가 Git index에 없다. 소스뿐 아니라 정책상 포함되는 문서 등도 포함한 수치다. 파일이 삭제됐다는 의미가 아니라, 로컬 결과를 현재 Git 체크포인트만으로 재현할 수 없다는 의미다.

주요 통과 범위는 producer 38개 case, manifest draft 11개, WAV 한도 5개, Studio sample review 9개, CLI 종단 3개와 언어·performance compiler·offline session·실제 CLAP host 검사다. 일부 case는 다른 타깃과 겹칠 수 있으므로 합계를 고유 기능 수로 환산하지 않았다.

별도 Python 회귀는 production draft parity, source admission, full-product gate, public-release state machine의 **33개 테스트가 9.367초에 통과**했다. 이는 게이트 구현의 선택된 회귀 증거이며 실제 후보의 출시 수락이 아니다.

### 3.3 실제 플러그인 행렬과 linked-engine smoke를 구분했다

현재 matrix는 실제 CLAP binary를 로드해 process하는 경로이며 **336/336 PASS**였다. 결과 자체가 `engineering`, `development-fixture`, `releaseEligible:false`라고 기록한다. 실제 음향 목표의 pitch 정확도나 사용자 노래 완성도를 뜻하지 않는다.

반면 이번 5초 soak smoke의 실행 경로는 **linked-engine-v1**이다. 플러그인 파일의 hash를 기록한다고 실행 대상까지 실제 플러그인이 되는 것은 아니다. 따라서 이것을 ‘실제 DAW 플러그인 장시간 안정성 통과’라고 보고해서는 안 된다. [각 실행 경로의 CMake 연결](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/phase12c/CMakeLists.txt:1)

전체 빌드 오류 때문에 기존 `seam_tests` 바이너리를 실행해 그 결과를 최신 core 검증으로 인용하지 않았다. 이전 날짜/이전 소스의 전체 회귀 숫자는 역사 자료로 보존하되 현재 결과와 합치지 않았다.

## 4. 잘하고 있는 부분: 안전 장치가 실제 제작 경로에 연결되고 있다

### 4.1 악보가 주인이고 렌더러가 소비자인 구조는 맞다

발음 identity, 음소 timing, pitch/dynamics/발음 연결과 수동 소유권을 공통 musical domain에서 계산하고, Sample/Procedural 자원이 이를 소비하는 구조가 있다. 이 구조를 유지하면 sample 선택의 우연에 노래 전체 의미가 종속되는 문제를 줄일 수 있다. 새 neural 경로도 여기에 연결하는 것이 맞으며, 별도의 독립적인 ‘neural 전용 악보’를 만드는 방향은 피해야 한다. [현재 렌더 분기와 공통 입력 소비](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/src/render_pipeline.cpp:143)

### 4.2 원본·가공본·검토본·설치 자원을 나눈 것은 필수적인 투자다

같은 WAV hash가 곧 같은 권한이나 같은 take를 뜻하지 않도록 source binding과 revision chain을 구분한다. 명시적인 검토, stale generation 거절, create-new publication, exact content identity는 실제 voicebank 제작 도구에서 필요하다. 이런 기반을 없애면 재생성된 소리가 예전 승인이나 기존 노래를 조용히 바꾸게 된다.

이번에는 종단 테스트가 실제 `create-sample-draft` 명령을 호출한다. fixture가 manifest와 locked pitch mark를 미리 직접 만들어 넘기는 경로가 아니다. 출력된 draft는 추정 marker/pitch로 시작하고, 검토·candidate·패키징·설치 이후 producer와 원본을 숨긴 상태에서 새 악보를 재개방해 Final 및 master/stem WAV를 만든다. [실제 draft 생성](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_sample_review_cli.cpp:46), [설치와 새 노래 출력](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_sample_review_cli.cpp:140)

**따라서 ‘소리를 생성하고 편집해서 데이터 bank로 만드는 개발이 전혀 진행되지 않았다’는 평가는 현재 코드에 맞지 않는다.** 다만 해당 fixture는 짧은 sine 음원, 사전 설정한 source qualification, 테스트 signing key와 두 음의 ‘あ’ 악보다. 실제 여성 가수나 미지의 일반 문장 발음 수락과는 다르다.

### 4.3 최근 복구·무결성 수정은 유지해야 한다

미완료 journal로 생긴 generation gap을 무조건 손실로 취급하던 경로에 parent/aborted ancestry 증거가 추가됐다. 정상적인 중단 복구는 진행하면서, 사라진 committed history를 임의로 면책하지 않는 방향이다. 이번 producer 회귀가 통과했다. [복구 ancestry 기록](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/repository.cpp:240)

sample draft 생성은 실제 음원 bytes의 hash를 확인하고 bounded decode를 수행한다. Studio도 hash를 확인한 동일 bytes를 decode하도록 바뀌었고 resize는 pinned 분석 결과의 geometry만 다시 배치한다. 이 수정은 소스에서 확인했으며, 최신 추가 Studio draft 회귀는 앞서 설명한 컴파일 오류 때문에 아직 실행 수락하지 않았다. [현재 로딩 순서](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:338), [resize 경로](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:455)

## 5. 가장 먼저 고칠 사용자 위험: 샘플 편집 후 창을 닫으면 변경 보호가 빠진다

**P1, 소스 경로로 확인한 작업 손실 위험이다.** native Studio의 `requestClose()`는 `allowDesignerReplacement()`만 호출한다. 이 함수는 Designer가 없거나 clean이면 닫기를 허용한다. Sample Editor의 marker/pitch 편집은 별도 controller의 `dirty_`를 설정하지만 이 종료 조건에 포함되지 않는다. [종료 처리](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:1181), [Designer만 검사하는 조건](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:82), [샘플 dirty 설정](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:462)

AppKit의 실제 창 닫기 delegate가 이 메서드를 호출하며, controller 소멸자는 worker를 정리할 뿐 dirty manifest를 저장하지 않는다. 따라서 **draft 열기 → sample marker/pitch 수정 → 저장하지 않고 일반 창 닫기**에서 변경이 보호되지 않는 경로가 성립한다. 이번에는 실제 사용자 파일로 유실 실험을 하지 않았다. 녹음 종료 시 WAV 저장은 별도 경로이므로 ‘모든 녹음이 유실된다’로 확대하지 않는다. [실제 close delegate](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/native_window_appkit.mm:732), [controller 종료](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio_production_project.cpp:268)

수정은 단순 경고문 추가보다 넓어야 한다.

1. Designer와 sample manifest의 dirty/busy 상태를 함께 다루는 종료 의사결정 경로를 만든다.
2. Save / Discard / Cancel을 제공하고, 저장 실패·저장 중·modal 이후 revision 변경에는 닫기를 허용하지 않는다.
3. 일반 창 닫기와 메뉴 종료가 같은 정책을 사용하도록 한다.
4. 저장 후 재실행한 manifest의 실제 marker/pitch 값까지 확인한다.

새 DSP나 외형 확장보다 먼저 처리할 가치가 있다. 사용자가 만든 편집 결과를 잃는 제품은 소리가 좋아도 제작 도구로 신뢰하기 어렵다.

## 6. 제작 pipeline의 다음 공백: source 평가와 다중 스타일·음소별 QC

### 6.1 ‘사용해도 되는 source’와 ‘출시 후보로 충분한 source’의 구분은 옳지만, 완료하는 작업이 부족하다

실행 정책은 source-use/transformation 권한과 증거를 검사한다. candidate qualification은 추가로 redistribution/commercial-render 권한, coverage/listening 평가 등을 검사한다. 생성이나 unit review가 이 조건을 자동 승인하지 않는 것은 올바르다. [source 정책](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/project.cpp:14), [candidate qualification](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/project.cpp:59)

그러나 현재 CLI의 init/create/prepare/inspect/review/publish 경로만으로는 실제 평가 자료를 등록하고 재평가하는 작업이 완성되지 않는다. 종단 fixture는 source coverage/listening을 처음부터 PASS로 둔다. 따라서 현재 연결성 증거에는 **‘사전에 준비된 source 조건’이라는 전제**가 있다. [현재 명령 집합](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-cli/sample_review_commands.cpp:202), [fixture의 전제](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_sample_review_cli.cpp:31)

필요한 다음 구현은 평가 대상 source revision, 수행자, 근거, 적용 범위, 결과를 명시적으로 기록하는 command/repository transaction이다. 기존 take의 불변 attribution과 현재 정책 변경을 구별하고, 잘못된 source 변경에는 영향받는 검토·candidate를 무효화해야 한다. JSON을 직접 고치거나 gate를 느슨하게 해서 PASS로 만드는 것은 해결이 아니다. 실제 권한 판단은 자료와 적절한 검토가 필요하며, 테스트의 boolean이 그 판단을 대신하지 않는다.

### 6.2 단일 스타일 draft 성공이 전체 bank 생산 완료는 아니다

현재 `UnitAssignment`는 coverageKey/pitchLayer를 중심으로 하고 language/style identity가 없다. candidate publication은 다중 style을 명시적으로 거절한다. 반면 목표는 언어·음역·distinct style·paired blending까지 포함한다. [assignment 모델](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/include/seam/voicebank_production/project.hpp:117), [다중 style 거절](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/repository_candidate.cpp:474)

U10은 안정된 language/style/phone/pitch assignment identity, migration, generation mapping, review ownership, candidate mapping을 같이 바꿔야 한다. draft unit ID에 style이 들어간다는 사실만으로 producer migration이 끝난 것은 아니다. Paired blend도 이름이 같은 unit 두 개를 섞는 수준이 아니라 발음·timing·정렬의 호환성과 음향 결과가 필요하다.

### 6.3 무음·폐쇄·호흡에 모음용 QC를 일률 적용하면 안 된다

candidate 경로는 finite mono audio와 함께 RMS가 일정 수준보다 높을 것을 요구한다. 이런 공통 검사만으로는 pause/closure/breath/voiced sustain의 목적 차이를 나타낼 수 없다. 또한 candidate publication에는 기본 WAV decoder를 쓰는 호출이 남아 있어, 새 bounded API를 추가한 사실을 전체 intake의 한도 통일로 확대하면 안 된다. [현재 candidate audio 검사](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/repository_candidate.cpp:532)

U12에서는 unit kind별로 pitch 검사의 적용 여부, 의도된 silence/closure, voicing, 잡음/클리핑, boundary, duration을 정의해야 한다. unsupported 상태를 무조건 통과시키는 것이 아니라, **검사의 적용 조건을 정확하게 만드는 것**이 핵심이다.

## 7. Voice Designer: 실제 음색 편집은 있으나 일반 여성 가수의 발음 모델은 부족하다

**현재 Designer를 가짜 UI라고 평가할 이유는 없다.** phonation open quotient, tilt, aspiration, jitter/shimmer, formant와 frication 값을 편집하고, recipe를 저장하며, 선택 pose의 소리를 만들고 generation 입력으로 전달하는 코드가 있다. ‘신디사이저를 깎듯 목소리를 만든다’는 요구를 올바르게 제품화하기 시작했다. [실제 조절 경로](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:151), [generation으로 전달](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:186)

그러나 현재 articulation의 실체는 **OralVowel과 Frication 두 종류**다. 모음이 아닌 소리는 명시적인 binding을 가진 unvoiced onset에 한정되고, voiced consonant/coda 등은 거절된다. nonzero nasal coupling도 지원되지 않는다. [gesture 정의](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voice-design/include/seam/voice_design/articulation_plan.hpp:8), [실제 허용 조건](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voice-design/src/articulation_plan.cpp:79), [비음 결합 제한](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voice-design/src/vocal_tract.cpp:16)

중요한 예는 영어 `sing`이다. 언어 코드에서 `ng`를 올바르게 Coda로 바꿨더라도 procedural 엔진이 그 비음/종성을 실제 발성할 수 있게 된 것은 아니다. **언어 정답과 음향 구현은 별도의 완료 조건이다.**

따라서 다음 단계는 모든 자음을 noise pose로 등록하는 것이 아니다. closure/burst/release, 유성 자음, 비음·유음, 종성, 음소 사이 전이와 발성 연속성을 실제 gesture/tract/phonation 모델로 구현하고, 언어별 단어와 문장에서 검증해야 한다. 단순한 formant 상승이나 기본 pitch 변경으로 ‘여성 가수 정체성’을 수락할 수도 없다.

현재 audition은 선택한 pose/pitch의 sustained 음을 중심으로 한다. 일반 가사의 phrase audition과 bank로 구운 뒤의 재생 결과를 같은 비교 작업에서 들을 수 있어야 한다. [현행 audition 단위](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voice_designer_audition.cpp:39)

실질적인 다음 성과물은 ‘슬라이더 몇 개 추가’가 아니라 **동일한 원본 캐릭터가 여러 모음·자음·음역에서 정체성을 유지하며, 보지 않은 짧은 가사를 알아들을 수 있게 부르는 pilot**이다. 이 pilot은 Full-Scope 범위를 축소하는 출시판이 아니라, 실패 원인을 조기에 드러내는 개발 도구다.

## 8. Classical renderer와 표현: 기본 제어는 실재하고, 고급 제어는 아직 열려 있다

현재 renderer capability가 지원으로 선언하는 제어는 pitch, timing, dynamics, vibrato, attack, release다. formant, breathiness, tension, airiness, gender, style blend, growl은 이름이 있어도 지원으로 활성화되지 않는다. [현행 capability](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-synthesis/src/renderer_capabilities.cpp:9)

**Recipe의 formant 값을 바꾸는 기능과 노래 도중 formant lane을 편집하는 기능은 다르다.** proposal/ownership 저장 구조를 만들었다고 실제 표현 DSP가 전부 완성된 것도 아니다. 반면 기존 vibrato 등의 UI → command → performance compiler → frequency → 음원 소비 경로는 존재하므로 모든 expression을 장식이라고 평가하는 것 역시 잘못이다.

필요한 작업은 각 필수 표현에 대해 지원 backend/resource, neutral 값, 음향상 기대 변화, pitch/timing 부작용을 정의하고 **저장 → 재개방 → Final PCM 변화 → undo → cache identity**까지 연결하는 것이다. 모든 renderer가 모든 표현을 지원할 필요는 없지만, 필수 표현이 제품 어디에서도 지원되지 않는 상태로 Beta GO를 선언할 수는 없다.

Classical 품질도 단일 모음이나 sine 결과만으로 평가하면 안 된다. voiced/unvoiced onset, sustain, release와 unit join을 구분해 실제 CV/VC·빠른 발음·도약·long sustain에서 pitch/timing과 경계 잡음을 비교해야 한다. 이 보고서는 이번에 CV/VC의 cents 오차나 listener 점수를 새로 측정하지 않았으므로 ‘특정 음질 지표 실패’를 수치로 단정하지 않는다.

또 하나의 연결 공백은 harmony다. `AddHarmonyCommand`는 같은 region에 동시 note를 추가하지만, procedural snapshot은 겹친 note를 직접 단성 compiler에 전달하며 그 compiler는 명시적 voice allocation이 없으면 거절한다. **Harmony 명령 구현과 procedural harmony의 audible 출력은 아직 같은 완성 기능이 아니다.** 같은 source/ownership 계약을 유지하면서 voice 분리·render·mix를 연결하는 종단 회귀가 필요하다. 이번 판단은 소스 경로 확인이며 별도의 harmony PCM 재현 실험을 수행한 것은 아니다. [harmony 추가](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-application/src/harmony_commands.cpp:99), [procedural compiler 호출](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/src/render_snapshot.cpp:451), [겹침 거절](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-synthesis/src/performance_compiler.cpp:285)

## 9. 언어와 neural: interface 등록을 실제 가수 능력으로 집계하면 안 된다

### 9.1 세 언어의 호출 경로가 생겼지만 일반 가사 품질은 별도다

English의 syllable boundary/onset cluster/terminal coda와 실제 timing 연결 회귀는 이번 언어 타깃에서 통과했다. Korean에는 Hangul 분해와 경계 규칙이 있고 shared resolver의 identity·입력 한도도 존재한다. 이는 의미 있는 기반이다. [영어 역할과 timing 회귀](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_english_phonemizer.cpp:227)

그러나 English는 작은 bootstrap 사전과 미등록 단어의 철자 기반 추정에 의존한다. Korean의 경계 규칙은 형태소/어휘 예외 처리가 완성된 언어 엔진이 아니며, 기본 Japanese kana 경로도 일반 한자 가사의 독음 해결을 대신하지 않는다. 별도의 reading resource/helper 경로가 있다고 실제 설치된 리소스 선택과 모든 UI 소비자가 자동 완성되는 것은 아니다. [영어 사전과 추정](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-phonemizer/src/english_phonemizer.cpp:25), [한국어 규칙의 경계](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-phonemizer/src/korean_phonemizer.cpp:94)

언어별로 미등록 가사 비율, 잘못된 독음·강세·받침, 수동 수정에 드는 작업과 audible render를 측정해야 한다. 같은 region 내 혼합 언어가 현재 거절된다는 점도 기능 범위에 명시해야 하며, 그것을 자동으로 전체 필수 요구라고 새로 확대하지는 않는다. 승인된 3개 언어 지원을 검증하는 것과 모든 code-switching까지 새로 약속하는 것은 다르다.

### 9.2 Neural은 실제 모델·가사 입력·제품 pipeline을 연결해야 한다

현재 helper 실행, bounded framing, request/response identity, deadline/cancellation 경계는 있다. 그러나 `NeuralRequest`에는 pronunciation hash, F0, dynamics가 있고 실제 phoneme sequence·duration·language·speaker/style conditioning은 없다. **hash는 모델이 불러야 할 가사의 내용을 대체하지 못한다.** [현재 request 계약](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-neural-synthesis/include/seam/neural_synthesis/worker_protocol.hpp:23)

일반 `PhraseRenderPipeline`은 Sample/Procedural 외 자원을 거절한다. 테스트 helper는 실제 훈련된 가수가 아니라 테스트 backend의 PCM을 만드는 probe다. 따라서 IPC 테스트 통과를 neural singing 구현 완료로 해석할 수 없다. [pipeline의 실제 분기](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/src/render_pipeline.cpp:155), [probe 출력](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/helpers/neural_worker_probe.cpp:15)

U35 데이터/권한/정렬/분할/학습 재현, U36 model/vocoder/음질 자격, U37 실제 inference·conditioning·설치 helper·Final/cache 연결은 서로 다른 작업이다. 이들은 병행할 수 있지만, 실제 모델 입출력에 근거하지 않은 protocol만 계속 확장해서는 안 된다. 반대로 qualified 모델이 아직 없다는 이유로 모든 제품 연결 작업을 중지할 필요도 없다.

## 10. 호스트: Fixed Audio의 진전은 인정하되 Follow Host 미구현은 남는다

실제 로드한 CLAP 플러그인의 cold score bounce 테스트가 이번에 통과했다. test는 live note event 없이 저장 score의 8개 note를 두 sample rate에서 확인하고, missing/failed Final과 beats-only 경로를 거절하는지 검사한다. 생성 WAV의 형식도 확인한다. 이것은 linked-runtime 테스트보다 강한 host boundary 증거다. [실제 host 검증 내용](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_clap_offline_host.py:23)

현재 `prepareOfflineRender()`는 **FollowHost를 명시적으로 Unsupported 처리**한다. 완전한 tempo map 없이 잘못된 Final을 성공으로 내보내는 것보다는 올바른 실패지만, Follow Host의 구현 완료가 아니다. [명시적 미지원](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-clap-editor/src/editor_runtime_project.cpp:181)

별도 Phase12B 회귀는 현재 exit 40으로 끝난다. 이는 기존 partial Final을 negative case로 바꾼 뒤 새 positive 편집을 준비하는 조건에 해당하며, 재실행에서도 같았다. 독립 reviewer가 기존 Release binary를 debugger로 두 번 추적한 결과, nucleus 조회와 boundary 변경은 성공했고 두 번째 `selectUnitVariant()`가 **“Unit plan entry is unavailable for this phoneme”**를 반환했다. [현재 종료 조건](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_phase12b.cpp:155), [반환 경로](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/src/technical_edit_controller.cpp:482)

원인은 invalid 편집을 undo한 뒤 완전한 새 unit plan이 비동기 publish되기 전에 positive fixture가 다시 선택을 시도하는 순서다. `currentTechnicalRenderView()`가 retained partial Ready 결과의 active unit plan을 공급할 수 있다. 수정 시에는 임의 sleep이 아니라 **undo → 현재 revision의 완전한 plan 준비 확인 → unit/renderer 선택 → 지원되는 boundary 변경** 순서와 명시적인 준비 조건을 시험해야 한다. 이것은 준비 상태/fixture 문제의 확인이며, 잘못된 unit이 적용되는 데이터 손상까지 재현한 것은 아니다. [비동기 view 소비](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/src/authoring_runtime.cpp:529)

이 실패와 cold-bounce 테스트의 성공은 동시에 성립한다. ‘CLAP 전체가 실패’나 ‘Final lifecycle 전체 통과’ 중 어느 하나로 단순화하지 않는다.

다음 구현은 authoritative timing source, host/project offset, tempo-map revision, range invalidation, Pending/Failed의 출력, offline preparation과 오류 전파를 한 계약으로 묶어야 한다. 순간 BPM만으로 과거 beat 위치를 환산하면 tempo change를 복원할 수 없다. 실제 여러 호스트의 API 차이와 지원 경계를 시험해야 한다.

현재 결과는 실제 서명·설치된 Windows/macOS의 9개 필수 DAW tuple, VST3/AUv2 배포와 장시간 안정성을 대신하지 않는다. 5초 linked-engine smoke를 장시간 host qualification으로 부풀리지 않는 것이 특히 중요하다.

## 11. Native UX와 캐릭터: 더 많은 화면보다 작업 보존·읽기·반응성이 우선이다

### 11.1 겹침과 overflow는 실제 편집 의미까지 검증해야 한다

노트 겹침 자체가 언제나 버그는 아니다. 다성부 악보의 실제 시간 관계를 유지하면서 어떤 note를 선택·수정하는지 분명해야 한다. 짧고 겹친 note의 hit target, 선택 순환, lyric 확인, drag/resize와 undo가 함께 검증 대상이다. 과거 지적을 근거로 현재 모든 겹침이 그대로라고 단정하지 않는다.

텍스트는 무조건 작게 줄여 한 줄에 넣는 방식으로 해결하면 안 된다. compact한 작업 밀도는 유지하되, 핵심 action/결정은 읽을 수 있게 하고 긴 hash/path·진단은 별도의 detail, wrap/scroll, copy, 키보드 탐색으로 전체 내용을 확인할 수 있어야 한다.

현재 새 Review 화면은 버튼·안내에 6.0, data/status에 7.0 크기를 전달한다. 중요한 검토 결정을 위한 가독성 위험으로 분류한다. 이것은 코드 값의 확인이며, OS 실제 픽셀 크기나 모든 DPI에서의 실패를 측정한 수치는 아니다. 이번 검토에서 최신 전체 UI를 새 화면 캡처로 인수하지 않았다. [현행 Review typography](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio_production_view.cpp:118)

### 11.2 resize는 개선됐지만 기본 unit 선택은 동기식이다

Review 화면의 이전/다음은 비동기 경로를 사용하지만 기본 rail 클릭/Up·Down은 `selectUnit()`에서 파일 읽기·hash·decode·spectrogram을 동기 수행한다. 최대 입력 한도가 있다는 것과 UI가 멈추지 않는다는 것은 별개다. 실제 freeze 시간을 이번에 측정하지 않았으므로 이는 소스상 responsiveness 위험이다. [동기 선택 경로](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:207), [무거운 처리](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:354)

기본 Editor도 같은 latest-request/stale-safe worker로 통일하고, 허용되는 큰 WAV에서 선택 응답·취소·창 닫기를 측정해야 한다. spectrogram FFT 내부 취소와 worker 종료 지연도 함께 봐야 한다.

### 11.3 캐릭터는 이미 상태 연동이 있으나, 노래의 performance 연동까지는 아니다

character state asset과 bank/card identity 검증, rendering/warning 등의 상태 선택이 있다. 따라서 ‘에셋 활용 코드가 전혀 없다’는 평가는 현재와 맞지 않는다. 특정 bank identity에 결속된 캐릭터가 임의의 새 bank에 안 보이는 것도 무조건 렌더 버그는 아니다. [실제 identity 연결](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voice_identity.cpp:6), [캐릭터 bank binding](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/assets/character-01/manifest.json:3)

남은 R14는 실제 설치 singer와 적절한 asset의 연결, playhead/음소 timing에 따른 mouth/performance 상태, reduced motion, collapse/off와 audio 독립성이다. 새 일러스트 수를 늘리는 것보다 이미 가진 asset이 **누가, 어떤 소리로, 지금 무엇을 하고 있는지** 정확히 전달하도록 연결하는 것이 우선이다.

## 12. 출시 gate: fail-closed 기반은 좋지만 증거의 의미 검증은 더 필요하다

현재 canonical contract는 **R1–R20의 20개 요구, 83개 case, 9개 host tuple**을 포함한다. 그러나 resource matrix와 evaluation profile은 `UNRESOLVED`, released resource는 **0개**, criterion은 **18개 FIXED / 11개 UNRESOLVED**다. 이 숫자는 이번에 JSON에서 읽은 계약 상태이며 source folder 안의 모든 음원 파일 개수를 뜻하지 않는다. [canonical contract](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/product/full-product-beta-contract.json:49)

실제 배포 fixture의 unit 여러 개가 같은 짧은 WAV를 참조하며, 해당 README도 release-quality singer 용도가 아니라고 명시한다. 그러므로 manifest에 phone 이름이 많거나 structural coverage가 채워져 있다는 사실만으로 그 발음을 실제 녹음/생성한 것으로 간주할 수 없다. [fixture의 명시적 용도](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/assets/demo-human-voicebank-public-domain/production-bank/README.md:3)

이번에는 기존의 좁은 operation 검증 probe를 현재 코드에 다시 실행했다. README 파일을 operation input/output/raw reference로 사용했을 때 `_observation_errors()`는 error를 반환하지 않았다. 현재 코드는 operation ID 집합과 reference의 경로/hash를 검사하지만, 그 파일이 해당 작업의 실제 결과인지까지 typed replay로 확인하는 수준은 아니다. [operation 검사](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/external_beta/full_product_report.py:376)

**이것은 전체 report 검증 또는 Beta GO를 우회했다는 증거가 아니다.** full validator는 별도로 schema·matrix·criteria·review 등의 조건을 요구한다. 발견한 것은 낮은 단계의 ‘증거가 실제 작업을 입증하는가’ 검증 공백이다.

U45에서는 operation별 입력/출력 형식, source/candidate/resource identity, 실제 실행 결과와 재현/측정 연결을 검사해야 한다. hash는 파일이 바뀌지 않았음을 확인하지만, 그 파일 내용이 주장에 적합함을 자동 증명하지 않는다. 실제 음향·creator 증거도 PASS 문자열과 동일 JSON claim의 복제로 대체하지 않아야 한다.

또한 ledger 최상단의 ‘Studio/CLI publication 및 package/install은 아직 open’ 문구는 현재 코드보다 뒤처졌다. 반대로 contract의 semantic validator `UNAVAILABLE` 표기와 report reference 누락을 설명하는 진단도 실제 validator 호출의 존재와 맞추어 정리할 필요가 있다. **미구현, 미제출, 미검증, 실패를 서로 다른 상태로 보고해야 한다.** [현재 gate 호출](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/external_beta/release_gate.py:159)

## 13. 실행 방식의 문제: 큰 변경 묶음과 검증 비용을 줄여야 한다

리뷰 산출물 추가 전 작업본에는 tracked 변경 215개와 untracked 파일 336개가 있었다. tracked diff만으로 23,809줄 추가·1,250줄 삭제이며 untracked 파일의 줄 수는 포함하지 않았다. 이것이 모두 쓸모없는 코드라는 뜻은 아니다. 다만 한 체크포인트로 검토·재현하기에는 큰 변경 묶음이고, 기능 하나의 수정이 전체 검증을 반복시키기 쉽다.

이번처럼 별도 agent가 작성한 테스트가 private API를 호출하고, 기존 회귀의 새 positive fixture가 즉시 실패하는 일은 **독립 모듈 작성 완료와 통합 완료를 구분할 필요**를 보여준다. 작성 agent의 ‘stable checkpoint’ 보고만으로 빌드 완료를 인정하지 않고 통합 실행을 거쳐야 한다.

다음부터는 한 작업 묶음마다 사용자 능력, 코드 소유자, 바뀐 identity/schema, 직접적인 negative/positive 검사, 완료 경계를 짧게 남기고 통합해야 한다. 병렬 작업은 공유 파일을 건드리지 않는 2–3개 목적별 lane 정도로도 충분하다. 이는 고정 생산성 수치가 아니라 현재 큰 dirty worktree에 대한 실행 권고다.

source closure를 초록색으로 만들기 위해 무조건 staging하거나 fixture/evidence를 자동 commit해서는 안 된다. reviewable checkpoint를 만들 때 필요한 코드·테스트·문서를 확인하고, private 음원/recipe/권한 자료와 generated build output을 구분한 뒤 publication해야 한다. 이번 리뷰에서는 staging·commit·push를 하지 않았다.

## 14. 다음 빅스텝: 구현 수가 아니라 한 가수의 제작·노래 완주를 목표로 한다

### 단계 A — 통합 실패와 작업 손실 위험을 먼저 닫는다

대상은 Studio 테스트 private API 호출, Phase12B exit 40, sample dirty close guard다. 기본 Editor의 비동기 selection도 같은 사용자 안정성 묶음으로 다룰 수 있다. 완료 조건은 관련 target과 전체 Release 빌드 성공, fresh 전체 CTest 결과, 저장 실패/취소/재실행의 실제 데이터 보존이다. 실패 테스트를 삭제하거나 unsupported Final을 묵인하는 방식으로 통과시키지 않는다.

### 단계 B — 실제 source에서 편집 가능한 단일 가수 pilot을 만든다

source 평가/재평가 command, 정확한 origin·take ownership, 생성/녹음/import, unit-kind QC, marker/pitch 수동 편집과 명시적 review를 연결한다. 파일을 손으로 고치지 않고, 실제 사용자가 draft에서 candidate까지 도달해야 한다. 초기에는 작은 진단 corpus로 결함을 빨리 노출하되, 이를 Full-Scope의 축소판 Beta로 부르지 않는다.

완료 증거는 **실제 source → draft → 편집 → 저장·재실행 → 독립 review → package → install → 원본 경로 없이 unseen lyric Final**이다. 현행 sine 종단 테스트는 계속 유지하되, 실제 phonetic material의 성공 증거를 추가해야 한다.

### 단계 C — pilot의 발음·음질 결함으로 DSP 우선순위를 정한다

비음/유성 자음/파열음/종성/전이, voiced transient, join, pitch range, distinct style을 같은 짧은 곡에서 비교한다. 일반적으로 필요한 범주를 구현하되, 다음 우선순위는 실제로 잘못 들리는 phone·구간에서 결정한다. 원본 여성 목소리의 정체성과 명료도를 보지 않은 문장에서 평가한다.

language/style assignment migration과 paired alignment는 이 단계의 제작 자료를 제대로 표현하도록 진행한다. 나중에 한꺼번에 이름만 붙이는 방식은 피한다.

### 단계 D — 실제 모델을 기준으로 neural과 필수 표현을 병행한다

학습용 데이터·정렬·분할·재현 기록, 실제 model/vocoder prototype, conditioning protocol, first-party native helper, sample/procedural과 같은 Final/cache/ownership 경로를 연결한다. neural 필수 scope를 뒤로 숨기지 않고 일찍 실제 입력·출력을 확인한다. timbral expression도 지원 backend에서 실제 PCM에 영향을 주는 경로를 끝까지 만든다.

### 단계 E — 가수와 곡이 있는 상태에서 native/호스트 경험을 완성한다

JA/EN/KO corpus, note/phoneme/expression 편집, import/export, Follow Host tempo 변경, full bounce 실패 전파, 읽기 쉬운 Review, 편집 보존, character performance를 실제 창작 작업으로 검증한다. host 순서는 개발상 좁혀 시작할 수 있지만 최종 9개 tuple과 두 플랫폼 의무는 유지한다.

### 단계 F — 실제 후보를 동결하고 Full-Scope 수락을 수행한다

실제 리소스와 측정 기준을 확정하고, acoustic/listener/creator 결과 및 보존된 U60/support 작업을 완료한다. 동일 서명·설치 후보로 장시간/host/복원 검증을 수행하고 typed product gate를 통과시킨다. 이때야 승인된 Beta GO라고 부를 수 있다.

이 순서의 핵심은 **B/C를 더 이상 먼 미래의 ‘asset 작업’으로 취급하지 않는 것**이다. 가수의 실제 발음과 음질이 불확실한 상태에서 D/E/F의 문서나 fixture만 늘리면 중요한 실패를 가장 늦게 발견하게 된다. 전체 범위는 유지하면서, acoustic pilot과 실제 model/data 작업을 engineering 연결 작업과 병행해야 한다.

## 15. 불확실성과 최종 판단

이번 검토는 현재 코드, 승인 계획, 실제 재빌드 및 집중 실행, 세 개의 독립 소스 검토 lane에 기반했다. 모든 파일의 모든 줄을 검토하거나 모든 제품 환경을 시험했다는 뜻은 아니다. 제품 코드·테스트·기존 계획은 수정하지 않았고 새 보고서와 재현 근거만 작성했다.

이번에 하지 않은 것은 fresh clean-checkout 전체 빌드, 최신 Studio draft 16개 실행, 최신 core 전체 실행, Windows runtime, 모든 native 화면의 시각/입력 QA, 실제 서명·설치된 9개 DAW 인수, 장시간 qualification, 실제 여성 singer 청취, 실제 훈련 neural singer 평가, 독립 creator/language/music 수락이다. 이 경계를 통과했다고 추정하지 않는다.

남은 중요한 질문은 사용자에게 지금 다시 선택을 요구하기 위한 것이 아니라 다음 실험이 답해야 할 항목이다.

- 첫 원본 가수가 어느 발음·음역에서 가장 크게 실패하며, 원인이 phonemizer·발성 모델·source·unit join 중 어디인가?
- recipe에서 들은 음색이 bake/편집/설치 후에도 의도대로 유지되는가?
- 실제 source 평가를 완료하는 데 필요한 자료와 사용자 조작이 제품 안에서 충분한가?
- 실제 모델이 필요로 하는 conditioning과 native CPU 시간·메모리 예산은 무엇인가?
- 자동 performance가 동일한 조건에서 수동 수정량을 줄이면서 남은 발음·timing 오류를 늘리지 않는가?

**최종 판단: ‘잘못된 프로젝트를 만들고 있다’기보다는, 올바른 구조의 제작 시스템이 실제 가수 제품보다 앞서 나간 상태다.** 최근 draft/review/install/Final 연결은 이 간격을 줄이는 좋은 변화다. 다음 큰 진척은 그 위에서 실제 가수와 곡을 완성하고, 발견된 사용자 작업 손실·통합 실패를 닫는 것이어야 한다. 지금 필요한 것은 새로운 축소 목표도, 또 다른 포괄적 상태표도 아니라 **현재 목표를 실제 소리와 사용자 작업으로 증명하는 실행 순서의 전환**이다.
