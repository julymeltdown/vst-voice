# Project SEAM 개발 방향 심층 재검토

## 1. 결론: 구조는 유지하되, 현재의 실행 순서와 완료 판단은 바로잡아야 한다

**프로젝트를 갈아엎을 이유는 없다. 하지만 지금 방식 그대로 기능을 계속 넓히는 것은 권하지 않는다.** 공통 성능 컴파일러, 불변 렌더 스냅샷, 편집 이력, 생성 작업의 취소·리비전 검증은 실제 사용자 경로에 연결되어 있다. 반면 “목소리를 만들고 → 편집 가능한 보이스뱅크로 출판하고 → 처음 보는 가사로 노래시키고 → DAW에서 다시 여는” 제품의 중심 과정은 아직 끊겨 있다.

이번 재검토에서 확인한 핵심은 다음과 같다.

- **검증 결과가 제품 동작을 과대 대표한 사례가 실제로 있다.** Phase 12C matrix에 CLAP 바이너리 대신 일반 텍스트 파일을 넣어도 336개 PASS가 나왔다. 플러그인 파일을 해시하지만 실제 실행하지 않기 때문이다.
- **핵심 코드의 미완성도 여전히 크다.** 제작 프로젝트의 export는 실제 bank가 아닌 템플릿을 출력한다. neural worker 계약은 있지만 일반 곡 렌더러는 neural resource를 거부한다. 고급 표현, 자음 합성, voiced transient 처리, 가수 종류별 동작 일치도 미완성이다.
- **새로운 기능 연결 과정에서 기존 안전 계약이 깨졌다.** 영어·한국어의 오래된 발음 편집 적용과 일본어→한국어 자동 재연결을 별도 프로그램으로 재현했다.
- **최근 전체 PASS 수치를 현재 빌드 상태로 사용할 수 없다.** 과거 전체 CTest 로그는 107/108 통과지만, 현재 작업 트리의 core 테스트 재빌드는 새 CLAP 헤더 포함 방식 때문에 실패한다.
- **다음 빅스텝은 구현량 확대가 아니라 한 개의 완성된 제작·노래 경로 확보여야 한다.** 동시에 신경망 데이터·학습 작업을 별도 필수 축으로 진행해야 하며, CLAP 검증을 그 진척으로 계산하면 안 된다.

판정은 **“아키텍처는 조건부 적절 / 실행 우선순위는 재집중 필요 / 현재 Full-Scope Beta GO는 NO-GO”**다. NO-GO는 출시 승인 불가라는 뜻이지, 모든 개발을 멈추라는 뜻이 아니다.

기존 DEVELOPMENT_DIRECTION_REVIEW_2026-09-09.md의 “방향은 GO” 판정을 이 문서로 수정한다. 특히 “실제 CLAP를 로드했다”, “다국어 공통 경계가 닫혔다”, “가장 큰 미완성은 코드가 아니다”라는 해석은 현재 근거보다 낙관적이었다.

## 2. 검토 범위와 숫자의 의미

검토일은 2026년 9월 9일이다. 대상은 codex/production-readiness-completion 브랜치의 로컬 작업 트리이며, HEAD는 741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d다. HEAD만으로 현재 구현을 재현할 수는 없다. 많은 변경이 아직 커밋되지 않았다.

기준은 승인된 Full-Scope Beta GO 계획과 그 원본 가상 가수 보고서다. 계획은 48개 구현 단위, R1–R20, V01–V18의 전체 범위를 유지한다. 일본어 우선 진행은 개발 순서이지 축소 Beta의 허용이 아니다. 근거: docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:17, :25, :41.

보고서에서는 근거를 구분한다.

- **실행 확인:** 이번에 수행한 빌드, 테스트, 반례 진단의 관측 결과.
- **소스 확인:** 현재 분기·필드·함수 호출로 직접 확인한 동작 또는 미구현.
- **과거 기록:** 이전 실행 로그나 원장에 남은 결과. 현재 버전의 재검증을 뜻하지 않는다.
- **추론·미검증:** 청감 심각도, 실제 DAW 영향, 외부 자원 상태처럼 이번 실행만으로 확정할 수 없는 판단.

문서와 소스가 충돌할 때는 실제 실행 경로를 우선했다. 전체 파일에 대한 보안 보증이나 모든 플랫폼의 재인증은 수행하지 않았다.

### 완료율을 다시 읽는 방법

실행 원장은 U1–U5를 로컬 수락, U6–U48을 미완료로 기록한다. 따라서 **원장에 기록된 단위 수 기준은 5/48 = 10.4%**다. 하지만 이것은 동일 가중치의 행정적 완료율이지, 남은 인력·기간·코드량·제품 품질의 추정치가 아니다. U6 이후에 이미 많은 부분 구현이 있으므로 “코드가 10.4%밖에 없다”는 해석도 틀리다.

더구나 이번에 U4 관련 회귀를 발견했으므로 5개 모두의 현재 수락 상태가 유지되는지는 영향 범위 재심사가 필요하다. 임의로 새 완료율을 부여하지 않는다. 근거: docs/implementation/FULL_SCOPE_BETA_EXECUTION.md:522.

과거 전체 CTest의 **107/108 = 99.1%**는 등록된 회귀 테스트 통과율일 뿐이다. 이 비율을 제품 완료율이나 Beta 달성률로 제시해서는 안 된다. 과거 대화의 95%, 약 30%, 구형 계획의 4/15 등은 분모와 검증 범위가 달라 직접 연결할 수 없다.

## 3. 검증 재실행: 현재 확인된 것과 더 이상 주장할 수 없는 것

이번에는 과거 전체 CTest 로그를 보존하고, 관련 타깃을 재빌드한 뒤 개별 실행 파일과 검증기를 직접 실행했다.

| 확인 항목 | 이번 결과 | 해석 |
|---|---|---|
| core 포함 Release 재빌드 | 실패 | 새 CLAP ABI 테스트의 외부 헤더 포함 방식에 따른 경고 오류 |
| language phonemizer | 5/5 통과 | 기존 언어 테스트 범위는 정상이나 아래 반례는 검출하지 못함 |
| Voice Design DSP·recipe | 12/12 통과 | 실제 procedural 기반이 존재함 |
| performance snapshot | 42/42 통과 | 여러 렌더 경로·불변성·음정·캐시 테스트가 실제로 통과 |
| neural worker protocol | 5/5 통과 | IPC·형상·identity 검증이며 학습 모델 품질 증거는 아님 |
| 강화된 canonical verifier 단위 테스트 | 15/15 통과 | 불충분한 증거를 거부하는 검증기 테스트 |
| full-product report 단위 테스트 | 5/5 통과 | 보고서 검증기 테스트이며 실제 제품 보고서 승인 아님 |
| 기존 matrix·smoke를 강화된 검증기로 확인 | 거부, exit 3 | 실행 경로·개별 행·유효 source identity 등이 부족 |
| 일반 텍스트를 plugin으로 준 기존 matrix 실행 | 336/336 PASS, exit 0 | matrix가 플러그인 실행을 검증하지 않는 반례 |
| 언어 편집 진단 | 2개 언어의 stale 편집 및 언어 전환 문제 재현 | 실제 라이브러리에 연결한 별도 진단 |
| source closure | 실패, 미색인 입력 265개 | 보고서 파일 생성 전 스냅샷 기준 |
| git diff --check | 통과 | 공백 검사이며 컴파일·기능 보증 아님 |

네 C++ 집중 실행의 합계는 64개 통과다. 이 합계를 “64개 제품 요구사항 완료”로 바꾸지 않는다.

과거 전체 로그는 build/release/Testing/Temporary/LastTest.log에 있으며 9월 9일 00:03–00:05 KST 실행, 108개 테스트 중 source closure 하나가 실패한 기록이다. 이 결과는 아래의 중단된 ABI 수정과 강화된 검증기 반영 전 기록이다. build-release-current 디렉터리의 로그는 8월 31일 것이므로 “current”라는 이름만 보고 최신 근거로 사용하지 않았다.

### 진행 중 수정의 경계

리뷰 요청으로 전환될 때 이전 구현 작업에서 시작한 변경 네 파일이 이미 남아 있었다. 두 파일은 canonical verifier와 그 테스트이고, 두 파일은 CLAP ABI 헤더와 이벤트 테스트다. 추가 구현을 중단했고 이 변경들을 되돌리지 않았다.

현재 core 빌드 실패는 tests/test_phase12c_clap_events.cpp:4의 새 직접 포함이 third_party/clap/include/clap/clap.h:65의 C 스타일 캐스트를 프로젝트의 -Werror,-Wold-style-cast 아래로 가져오기 때문이다. 수정 방향은 외부 헤더 경계를 일관되게 구성하고 다시 빌드하는 것이지 전체 경고 정책을 낮추는 것이 아니다. 이번에는 리뷰 범위를 지켜 수정하지 않았다.

## 4. 유지할 가치가 있는 구조: 실제 소리까지 이어지는 기반은 있다

### 4.1 악보의 의도를 샘플 선택과 분리한 것은 올바르다

일반 authoring 렌더 요청은 ProductionProjectRenderer와 ProductionRegionRenderer를 거친다. 스냅샷 생성 단계에서 발음·타이밍·성능을 고정하고, 같은 compiledPerformance를 실제 DSP에 넘긴다.

근거: libs/seam-authoring-runtime/src/render_coordinator.cpp:526, libs/seam-rendering/src/project_renderer.cpp:170, libs/seam-rendering/src/render_snapshot.cpp:807, libs/seam-rendering/src/render_pipeline.cpp:213.

Raw의 음정 계산과 PSOLA의 목표 F0 사용이 실제로 존재하므로, 기본 expression이 단순 저장 필드나 UI만인 것은 아니다. 다만 모든 구간·표현·backend가 동일하게 구현된 것은 아니다.

### 4.2 편집한 프로젝트와 이미 렌더 중인 작업을 분리한 것도 옳다

스냅샷 identity는 프로젝트 내용, rate, quality, style, 알고리즘 revision, audio/alignment identity 등을 포함한다. 최신 요청인지 검증한 후 결과를 공개하는 구조도 있다. 이는 undo 후 잘못된 소리가 되살아나거나 오래된 작업이 새 편집을 덮는 위험을 줄인다.

근거: libs/seam-rendering/src/render_snapshot.cpp:334, libs/seam-authoring-runtime/src/render_coordinator.cpp:443. 이번 42개 snapshot 테스트 통과는 이 기반의 일부를 지지한다. 다만 아래의 cache 진단 값 손실처럼 metadata 일치에는 결함이 남는다.

### 4.3 Voice Designer는 실체가 있다

발성·포먼트·마찰음 설정, seed, undo/redo, 비동기 미리듣기, A/B reference, recipe 저장·재열기, 생산 작업으로 전달하는 UI 경로가 있다. PhonationSource는 성능 평가기의 목표 주파수를 읽고, 성도 처리 뒤에 gain/gate를 적용한다.

근거: libs/seam-native-ui/include/seam/native_ui/voice_designer_session.hpp:10, apps/seam-voicebank-studio-native/main.cpp:186, :352, libs/seam-voice-design/src/phonation_source.cpp:41, libs/seam-voice-design/src/procedural_renderer.cpp:195.

따라서 사용자가 원한 “신디사이저처럼 목소리를 깎기”는 방향상 이미 반영되어 있다. 정확한 현재 표현은 “제한된 발음 범위의 절차적 음색 설계와 재생”이지 “완성된 여성 가수 제작기”가 아니다.

### 4.4 제안된 표현과 사용자가 수락한 표현을 나눈 것은 중요하다

자동 생성 결과를 곧바로 프로젝트에 덮지 않고 PerformanceTake로 제안한다. 수락할 때 source/revision을 확인하고 manual ownership을 적용하는 구조는 보존해야 한다. 근거: libs/seam-synthesis/src/automatic_performance.cpp:35, libs/seam-application/src/performance_job_context.cpp:60, libs/seam-synthesis/src/performance_compiler.cpp:436.

현재 생성기는 규칙 기반의 제한된 제안이다. 이 구조의 존재와 자연스러운 자동 가창의 달성은 별개다.

## 5. 최우선 결함: 테스트가 실행 경계를 건너뛰고 있다

### F01 — P1: “canonical CLAP matrix”가 CLAP 바이너리를 실행하지 않는다

**소스 확인과 반례 실행으로 확정했다.**

phase12c/src/matrix_runner.cpp:60은 별도로 링크한 VoiceEngine을 생성하고 :75에서 직접 process를 호출한다. plugin 인자는 canonical_evidence.cpp:130에서 해시할 뿐, runner에는 dlopen/플러그인 factory/init/activate/process 호출이 없다. phase12c/CMakeLists.txt:4–15도 matrix를 seam_live_voice에 링크한다.

336은 6 sample rates × 7 block sizes × 4 channel counts × 2 bend 값이다. 마지막 축은 CLAP/MIDI dialect 두 종류가 아니다. 각 조합은 한 블록만 렌더한다. PASS 조건은 유한값과 case 수이며, totalEnergy를 계산해도 PASS 조건에 넣지 않는다. 무음이 전부 finite여도 이 조건은 통과할 수 있다.

이번 반례는 텍스트 파일 not-a-plugin.clap을 --plugin으로 주고 같은 bank로 실행했다. 결과는 다음과 같다.

~~~text
cases=336 fail=0 energy=57.5934
result=PASS
~~~

이 테스트는 엔진 smoke로는 유용하지만 플러그인 호환성 증거가 아니다. 파일 해시가 정확해도 그 파일을 실행했다는 사실까지 증명하지는 못한다.

**필요한 변경:** 배포할 번들 자체를 로드하는 작은 독립 host를 만들고, 생성·활성화·상태 저장/복원·process·종료를 실행한다. 개별 조합별로 note 시작 전 무음, 시작 offset, 유의미한 출력, note-off 이후 종료, pitch bend 효과, 채널 독립성을 기록해야 한다. 잘못된 binary는 로드 단계에서 실패해야 한다.

강화된 Python verifier는 현재의 요약-only matrix와 smoke를 거부한다. 이는 올바른 변화지만, 실행 host를 아직 대체하지 못했다. 또한 향후 JSON에 executionPath 문자열을 추가하는 것만으로도 완료가 아니다. runner 구현·실행 로그·정확한 binary identity가 함께 검증되어야 한다.

### F02 — P1: CLAP live 출력은 왼쪽 채널을 모든 채널로 복제한다

**소스의 확정적 출력 경로 문제다. 이번에 실제 DAW로 청취하지는 않았다.**

plugin_entry.cpp:610–623은 liveOutputs[0][frame]만 읽는다. writeOutput():426–442는 그 scalar를 모든 출력 채널에 더한다. 엔진이 left/right를 별도로 계산해도 마지막 adapter가 차이를 없앤다.

별도로 CLAP PAN의 원래 범위는 0=왼쪽, 0.5=중앙, 1=오른쪽인데, plugin_entry.cpp:462, :474는 변환 없이 내부 [-1,1] Pan으로 넘긴다. 엔진의 pan 계산은 phase12c/src/live_voice.cpp:325, :527에 있다. [고정된 upstream CLAP 규격](https://github.com/free-audio/clap/blob/195b42a004144fab0b3cf95e9c067187d15365b7/include/clap/events.h#L163-L205).

**필요한 변경:** channel별 live sample을 유지하는 mix, 명시적 mono/multichannel 정책, protocol 범위 변환을 adapter에 둔다. 실제 binary의 stereo left/center/right 출력 에너지와 mono 가청성을 검사해야 한다.

### F03 — P1: CLAP 이벤트 ABI가 upstream과 달랐고, 수정은 아직 통합되지 않았다

리뷰로 전환되기 전 로컬 header는 note-expression 필드 순서와 일부 expression ID가 고정된 upstream과 달랐다. 기존·정상 구조가 둘 다 40 bytes라 sizeof 검사만으로는 차이를 잡을 수 없었다. 외부 host의 바이트를 잘못된 note 주소나 expression으로 읽을 수 있는 문제다.

현재 working tree에는 expression_id 위치와 enum 값을 고친 수정안, 독립 wire-offset 검사가 추가되어 있다. 근거: third_party/clap/include/clap/clap.h:353, tests/test_phase12c_clap_events.cpp:14. 하지만 새 테스트의 include 경계 때문에 core 빌드가 실패하므로 “ABI 수정 완료”로 판단하지 않는다.

**필요한 변경:** 외부 헤더 포함·dependency digest를 일관되게 정리하고, upstream wire layout을 독립적으로 구성한 이벤트를 실제 plugin에 전달한다. 동일한 잘못된 헤더를 host/test/plugin이 공유하면 세 곳이 함께 틀린 채 통과할 수 있다.

### F04 — P1/P2: live expression 주소와 callback 작업량도 검토가 필요하다

CLAP adapter는 expression.port_index != 0을 모두 거부하여 유효 wildcard -1도 거부한다. 내부 PitchBend는 noteId 지정 여부와 무관하게 channelBend를 바꾼다. 따라서 특정 음의 tuning이 같은 채널의 다른 음에 영향을 주는 경로가 있다. 근거: plugin_entry.cpp:455, phase12c/src/live_voice.cpp:302–305, :439.

또한 pluginProcess():568–595는 host가 제공한 eventCount 전체를 순회한다. 엔진 queue의 이벤트 상한과 별개로 adapter 자체의 callback 순회는 해당 상한으로 제한되지 않는다. 실제 host 부하 측정 없이 최악 지연을 단정할 수는 없지만, “엔진 최대 1,024개”만으로 전체 callback boundedness를 증명할 수 없다.

필요한 회귀는 같은 채널의 두 note-ID 중 하나만 tuning, port/channel/key wildcard, 동시간대 대량 이벤트, 잘못된 크기의 이벤트다. 이는 새 기능보다 공개 protocol 경계의 정확성 문제다.

## 6. 다국어 통합은 아직 기존 편집 보호 계약을 지키지 못한다

### F05 — P1: 영어·한국어는 오래된 sourceContextId 편집을 그대로 적용한다

English applyOverrides():103과 Korean applyOverrides():122는 unresolved 플래그와 형식은 검사하지만 실제 현재 발음의 sourceContextId와 비교하지 않는다. 공통 resolveLanguage():125도 원본 region을 그대로 adapter에 넘긴다.

일본어 resolver에는 base 발음을 다시 구해 context가 다르면 effective edit을 unresolved로 만드는 로직이 있다. 근거: libs/seam-phonemizer/src/pronunciation_resolver.cpp:149–159.

이번 진단은 현재 라이브러리에 연결하고, 유효한 길이지만 맞지 않는 64자리 context를 가진 override를 영어·한국어에 넣었다.

~~~text
en invalid_context_applied=1 orphan_warning=0
ko invalid_context_applied=1 orphan_warning=0
~~~

즉 “문맥 identity가 저장되어 있다”와 “실제 적용 전에 검사한다”가 다르다. 일반 편집 명령을 우회하는 저장 파일 복원·이전 버전 데이터·외부 변환에서는 이 방어가 특히 중요하다.

**필요한 변경:** 공통 resolver에서 입력 한도와 취소를 먼저 적용하고, override 없는 base pronunciation에 대한 context 일치를 검사한 뒤 effective edit만 adapter에 전달한다. replace/append, 오래된 lyric, resource 변경, 취소까지 회귀를 묶어야 한다.

### F06 — P1: 일본어→한국어 변경 시 기존 편집을 새 문맥에 자동 연결한다

lyric_commands.cpp:220–250의 compatible/uncertain 판단은 양쪽 언어가 서비스에 등록되어 있는지만 본다. language 또는 resource identity가 같은지는 보지 않는다. 음소 표기가 같으면 :316에서 새 context로 다시 묶을 수 있다.

단일 note의 일본어 あ에 문맥이 연결된 편집을 넣고 SetLyricCommand로 한국어 아로 바꾸는 실제 명령을 실행했다.

~~~text
ja_to_ko unresolved=0 context_replaced=1
~~~

자동 재연결은 서로 다른 언어의 같은 문자 a가 같은 샘플·seam 문맥이라는 잘못된 전제를 만들 수 있다. 음향적 동일성이 입증되지 않았는데 수동 편집이 검증된 것처럼 유지되는 것이다.

**필요한 변경:** 언어·resolver/resource identity가 바뀌면 기존 payload를 삭제하지 않고 unresolved로 보존한다. 명시적 재검토로만 새 문맥을 수락하도록 한다. unit/seam override와 undo도 함께 검사해야 한다.

### F07 — P2: 공통 진입점은 한도·취소 검사 전에 전체 입력을 스캔한다

language_resolver.cpp:223–225와 :250–251은 lyric map을 먼저 만든다. 실제 collection bound와 stop_token 검사는 내부 adapter 선택 후 resolveLanguage()에 있다. 중복 스캔도 발생한다.

pre-cancelled mixed-language 입력의 실제 결과는 취소가 아니라 “A region cannot mix explicit English, Korean and Japanese pronunciation services”였다. 이것은 취소 순서 오류를 재현한 것이며, 메모리 고갈을 실제 발생시켰다는 뜻은 아니다.

**필요한 변경:** 공통 진입점 최상단 admission, 중간 stop checks, 중복 인덱싱 제거, 마지막 input hashing의 취소 대응. 제한을 가진 helper가 있다는 사실보다 호출 전에 이미 수행하는 작업을 검토해야 한다.

### F08 — P2: 기술 편집 화면의 진단용 fallback과 실제 rebind 계약이 다르다

technical_edit_controller.cpp:73–85는 공통 resolver 실패 후 일본어 resolver로 review target을 만들 수 있다. 반면 실제 command의 context 검증은 공통 resolver를 사용한다. mixed region에서 화면이 제시한 일본어 target조차 command 단계에서 stale로 거부될 수 있다. 근거: libs/seam-application/src/lyric_commands.cpp:548.

fallback은 코드상 모든 generic failure 뒤에 시도하므로 “혼합 언어 진단에만 제한된다”는 이전 설명도 정확하지 않다.

해결은 진단용 target을 명시적으로 inspection-only로 만들거나, 공개된 rebind 계약과 동일한 지원 범위를 구현하는 것이다. 이번에는 소스 경로를 확인했으며 실제 native 화면의 클릭 재현은 하지 않았다.

## 7. 보이스 제작의 중심 단절: 만들어진 take가 설치 가능한 가수로 끝나지 않는다

### F09 — P1: producer export는 실제 bank가 아니라 제작 템플릿이다

ProductionProjectRepository::exportU57Inputs()는 제작 프로젝트를 저장·검증해도 status를 SYNTHETIC_READY_REAL_ASSETS_REQUIRED로 고정한다. 각 unit의 realAssetSha256은 빈 문자열이고 candidate는 BLOCKED다. 실제 출력은 production-brief.json과 candidate-template.json이다.

근거: libs/seam-voicebank-production/src/repository_export.cpp:50–56, :87–123. native producer의 export action은 이 함수를 호출한다: libs/seam-native-ui/src/voicebank_studio_production_project.cpp:985.

이 함수가 템플릿 역할을 한다는 사실 자체가 버그는 아니다. **Full-Scope U14에서 필요한 최종 출판 기능이 이 경로를 아직 대체하지 못한 것이 제품상 핵심 미완성**이다. 기존 bank packager/installer가 있어도, 검토한 take의 실제 audio·marker·pitch·provenance를 final manifest로 전환하는 단계가 자동으로 생기지는 않는다.

필요한 구현은 selected take와 immutable revision 고정 → 실제 자산 복사/검증 → unit/audio/marker/coverage manifest 작성 → package 생성 → 새 설치 위치에서 재해석 → 처음 보는 phrase 렌더다. “승인” 문자열을 붙이거나 기존 template의 BLOCKED만 바꾸는 것으로 해결하면 안 된다.

### F10 — 제품 출시 blocker: 실제 가수 자원은 기술 fixture 수준이다

현재 production demo manifest를 집계한 결과는 **8 unit / 고유 audio 경로 1개 / root MIDI 67 하나**다. 서로 다른 phone 선언이 같은 human-vowel-demo.wav를 참조한다. manifest 자체도 Public-domain Human Production Pipeline Fixture라는 이름이다.

근거: assets/demo-human-voicebank-public-domain/production-bank/manifest.json:6, :14–24. 전체 집계는 이번에 jq로 확인했다.

beta-voicebank-01-dossier.json은 sourceAssets와 derivedAssets가 비어 있고, package digest도 없으며 referenceSong이 NOT_RUN이다. 이는 모든 로컬 디스크에 가수가 절대로 없다는 주장이 아니라, **이 프로젝트에서 현재 검토 가능한 출시 자원·증적이 없다**는 판단이다.

공개 도메인 demo로 I/O·pitch mapping을 검사하는 것은 적절하다. 그러나 다른 phone 라벨을 한 WAV에 부여한 fixture로 자음 명료도, 여성 음색, 음역, 스타일 다양성을 검증할 수는 없다.

### F11 — P1: 절차적 음색 설계와 일반 가사 가창 사이에 큰 구현 공백이 있다

articulation_plan.cpp:79–104는 voiced oral vowel 또는 명시적으로 연결된 무성 onset frication을 처리한다. 유성 자음, 다른 역할의 phone과 노트 밖 문맥은 거부한다. nasalCoupling 필드는 있지만 VocalTract::create():16–17은 0이 아닌 값을 Unsupported로 반환한다.

따라서 “음색 조절이 없다”가 문제가 아니다. **조절한 음색으로 발음할 수 있는 음소·문맥이 좁은 것이 문제**다. 필요한 방향은 비음의 공명/반공명, 폐쇄·파열, 유성 자음, 전후 문맥에 따른 연결, release/coda, pitch/style 범위에서의 일관성을 음성 예제로 완성하는 것이다.

새 slider 수를 늘리는 것보다, 제한된 문장을 정확히 발음하고 그 범위를 넓히는 개발이 먼저다. 여성스럽고 독창적인 청감은 변수명이나 formant 값만으로 인증할 수 없으므로 실제 비교 청취가 필요하다.

## 8. 음악 엔진은 기본 경로가 있으나 Full-Scope 표현과 음질은 미완성이다

### F12 — P1: 고급 표현은 저장 타입과 실제 평가기 사이에서 끊긴다

도메인은 Breathiness, Tension, Airiness, Formant, Gender, StyleBlend, Growl 등을 표현할 수 있다. 하지만 accepted selection을 평가하는 performance_compiler.cpp:255–261은 Pitch/Dynamics/Attack/Release 이외의 채널을 Unsupported로 거부한다. automatic_performance.cpp:73–82도 같은 제한이다.

조용히 무시하지 않고 거부하는 것은 안전하다. 그러나 약속한 표현의 구현 완료를 의미하지는 않는다. Recipe의 고정된 aspiration/jitter/shimmer와 곡 재생 중 시간에 따라 변하는 expression lane은 다른 기능이다.

각 제어에 대해 저장 → 편집 → compilation → backend 적용 → cache provenance → export → 청취/측정의 연결을 닫아야 한다. 모든 backend가 모든 표현을 지원할 필요는 없다. 다만 각 필수 표현마다 실제로 지원하고 검증된 resource/backend 조합이 있어야 하며, 지원하지 않는 조합은 명확하게 진단해야 한다.

### F13 — P1 위험: PSOLA의 voiced attack/release는 완전한 목표 음정 처리가 아니다

classic_psola.cpp:164–186은 원본 기반 waveform을 먼저 채우고, PSOLA grain 처리는 sustain 구간에 적용한다. :227–230이 그 범위를 정한다. 승인 계획 U16은 voiced attack/release와 sustain 전체의 compiled F0 retarget을 요구한다.

따라서 sustain이 맞아도 voiced 양끝에 녹음 음정이 남거나 time mapping의 음정 효과가 생길 수 있다. **이 구현 차이는 소스로 확인했지만 청감 심각도는 이번에 측정하지 않았다.** 모든 자음을 무조건 pitch-shift하라는 처방도 아니다. voiced/unvoiced 구간을 구분한 목표가 필요하다.

annotated CV/VC source를 사용해 onset/sustain/release별 F0, timing, seam click과 청취를 검사해야 한다. 지속 사인파 테스트 통과만으로 이 요구를 닫을 수 없다. renderer_capabilities.cpp:20의 pitchPreservingTransient 의미도 실제 보장과 명확히 맞춰야 한다.

### F14 — P2: cache hit에서 fallback 횟수가 사라진다

region_renderer.cpp:231–248은 cold render의 fallbackCount를 구해 CachedPcm에 저장한다. 그러나 cache hit용 RegionRenderPhraseInfo는 :193에서 0으로 초기화되고 cached->fallbackCount를 복원하지 않는다. :210에서 그 0을 결과에 누적한다.

즉 같은 PCM을 재사용하면 fallback이 없어졌다고 집계될 수 있다. 실제 소리 변경이 아니라 설명·진단·증거의 일관성 문제다. source-traced finding이며 이번에 별도 cache 재현 프로그램은 실행하지 않았다.

수정 후 cold → memory hit → disk hit에서 PCM뿐 아니라 renderer identity, fallback count, diagnostics가 일치해야 한다. 향후 applied-controls와 timing provenance도 같은 기준으로 보존해야 한다.

### F15 — P1 미구현: 음향 연결을 고려한 unit selection과 paired style blending

unit_selection.cpp:56–64의 score는 pitch 거리, 길이 bonus, priority, take 번호다. :182–194는 위치마다 최적 상태 하나를 남기며 후보 비용을 더한다. 이전 sample 끝과 다음 sample 시작의 acoustic join 비용을 포함하지 않는다.

이는 안정적인 coverage selection이지만 U17의 문맥 의존 후보 상태와 다른 알고리즘이다. 두 style의 source set을 같은 성능으로 렌더한 뒤 blending하는 완결 경로도 이번에 확인되지 않았다. StyleBlend lane은 앞선 compiler 제한에 걸린다.

출구 기준은 “후보 선택 성공”이 아니라 같은 phrase의 연결 품질·동작 재현성·수동 강제 선택 보존을 측정하는 것이어야 한다.

### F16 — P1 미완성: sample과 procedural singer의 실제 실행 능력이 다르다

샘플 가수는 region_renderer.cpp:99–121에서 overlapping notes를 voice별로 분리한다. procedural project branch는 project_renderer.cpp:148–168에서 region을 바로 createProcedural()로 보내며, compiler는 overlap을 :285–288에서 거부한다.

또한 이 branch는 전체 region을 PhraseRenderPipeline으로 렌더하고 전달받은 region cache를 거치지 않는다. checkpoint/chunk helper가 존재하는 것과 native project 경로가 그것을 사용하는 것은 다르다.

같은 악보를 singer 종류만 바꿔 사용하려면 voice allocation, 출력 범위, cache/scheduler, 취소·재시도 정책을 실제 project entrypoint까지 맞춰야 한다. 이것은 모든 연산이 UI thread에서 일어난다고 단정하는 지적이 아니라, backend별 처리 경로 차이에 대한 지적이다.

## 9. 신경망과 출시 자원은 별도 필수 축이며 다른 작업으로 대체할 수 없다

### F17 — P1 미완성: neural IPC는 있지만 사용자 곡을 렌더하는 neural backend는 없다

runNeuralWorker()는 절대 경로 helper, 요청 검증, bounded process 실행, response identity 확인을 한다. 근거: libs/seam-neural-synthesis/src/neural_phrase_backend.cpp:10–43.

그러나 테스트 helper는 seam.test.neural.worker라는 이름으로 0으로 채운 PCM을 반환한다: tests/helpers/neural_worker_probe.cpp:13–20. 이것은 정상적인 protocol fixture지만 가창 모델이 아니다.

일반 PhraseRenderPipeline은 Procedural/Sample 외 resource를 거부한다: libs/seam-rendering/src/render_pipeline.cpp:158–160. 따라서 모델을 확보해도 제품 renderer에 꽂는 구현이 추가로 필요하다. 이번 검색에서 assets/build 아래 .onnx/.safetensors/.pt/.pth를 찾지 못했고 계획의 tools/voice_model_training 디렉터리도 없었다. 이 검색 범위 밖의 파일 존재까지 부정하지 않는다.

필요한 단계는 dataset·권리 및 source lineage → 학습/검증 split → alignment 검증 → 재개 가능한 checkpoint → acoustic/vocoder export → 실제 inference helper → snapshot과 preview/export 연결 → macOS/Windows CPU 검증이다. protocol PASS는 이 중 process 경계만 지지한다.

### F18 — P2: 실행 원장의 U35가 다른 계획의 의미와 섞였다

승인 Full-Scope 계획 U35는 “Neural dataset and training pipeline”이다. 하지만 실행 원장 :66–85는 CLAP Phase12C 검증과 runner promotion을 U35로 기록한다.

이 작업은 현재 계획의 U33 live expression/U34 host integration 및 release evidence에 연결할 수 있지만, neural dataset/training 완료 근거는 아니다. 구형 계획 번호를 사용할 때는 계획명을 붙여야 한다. 이 혼동은 실제 모델 작업이 전진한 것처럼 진행률과 우선순위를 왜곡할 수 있다.

이번 보고서는 승인 계획을 바꾸지 않는다. 필요한 것은 전체 원장을 다시 쓰는 일이 아니라, 잘못된 attribution 정정과 하나의 현재 단위 상태표다.

### 자원·독립 검증·설치 증거도 여전히 필요하다

현재 full-product-beta-contract.json은 R1–R20에 83개 case를 정의하지만 releasedResources는 0개, empirical criteria 11개는 UNRESOLVED다. 60 phrases/language, 3곡, 독립 creator 5명 등 고정 조건이 있는 것과 실제 자료가 수집된 것은 다르다.

이는 실제 evidence를 묶을 자원이 아직 고정되지 않았다는 뜻이다. 프로파일을 임의의 쉬운 수치로 채워 PASS시키면 안 된다. 정해진 권리·음악·언어 reviewer 역할도 구현자가 대신 승인했다고 표시할 수 없다.

캐릭터는 렌더·오류·재생 상태와 자산 상태를 연결한 개선이 있다. 그러나 phoneme 동기 mouth/pose와 production asset qualification은 남아 있다. 근거: docs/implementation/CHARACTER_PERFORMANCE_BINDING_2026-09-08.md:15. 이번에는 native UI의 노트 겹침·텍스트 overflow를 새로 시각 검증하지 않았으므로 디자인 완료나 재발 없음도 주장하지 않는다.

## 10. 개발 운영의 문제: 넓은 작업량이 완성된 사용자 경로로 수렴하지 않는다

보고서 생성 직전 Git 스냅샷은 변경 경로 446개였다. 그중 tracked 변경 181개, untracked 파일 265개이며, tracked diff만 19,757행 추가·808행 삭제다. 기존 사용자/다른 작업 변경이 포함되어 있으므로 전부 이번 작업 성과로 귀속하지 않는다.

경로별 분포는 libs 267, tests 86, docs 67, tools 9, phase12c 7, apps 4, scripts 2, third_party 2, root 2다. 이 분포는 기능 구현량이나 완료율이 아니라 **통합 검토 범위**를 나타낸다. HTML 보고서의 분포 그림도 같은 의미다.

많은 테스트와 문서가 있다는 것 자체는 문제가 아니다. 실제로 새 음정·타이밍·편집 보존 결함을 막아 왔다. 문제는 다음 세 가지가 함께 나타난다는 점이다.

1. 기능이 여러 단위에 걸쳐 부분 구현으로 늘어나는데, source-to-bank-to-song의 최종 단계는 template 상태다.
2. 공통 경계를 확장한 뒤 이 경계 바깥의 실제 consumer를 검증하지 않아, CLAP stereo와 언어 context 같은 오류가 남았다.
3. 수백 개의 미통합 변경 위에서 옛 PASS 기록과 최신 상태가 혼용되고 있다.

**필요한 운영 변경은 더 많은 계획 문서가 아니라, 다음 결과물의 출구 조건을 작게 고정하는 것이다.** 기존 코드를 폐기하거나 전체 프로젝트를 새로 구현할 이유는 없다. 검증기를 없애는 것도 해법이 아니다. 구현 경로를 통과하지 않는 검증을 걷어내고 제품 경로의 반례를 추가해야 한다.

## 11. 다음 빅스텝: 범위는 유지하고 순서를 바꾼다

한 번에 48단위를 모두 끝내겠다는 선언보다, 아래 순서로 사용자에게 보이는 완결 결과를 만들어야 한다. 이는 실행 권고이며 이번 리뷰에서 구현하지 않았다.

### 단계 A — 신뢰할 수 있는 출발점 복구

대상은 F01–F08과 F14다. 새 기능 추가 전에 현재 build 실패를 해결하고, 실제 binary host를 통한 최소 CLAP 행동 테스트, stale/cross-language 편집 보호, cold/cache metadata 동등성을 복구한다.

완료 조건:

- 엄격한 기존 warning 정책 아래 affected targets가 빌드된다.
- 텍스트 파일·잘못된 CLAP binary는 즉시 실패한다.
- 실제 plugin에서 stereo pan, note-ID별 tuning, note onset/off, state round-trip이 검증된다.
- 잘못된 영어·한국어 context가 소리에 적용되지 않는다.
- 언어/resource 전환에서 편집 payload를 잃지 않고 unresolved로 보존하며 undo가 정확하다.
- 과거 engine matrix는 engineering smoke로 명확히 구분된다.

여기서 2시간 soak부터 돌리는 것은 비효율적이다. 잘못된 실행 경로로 오래 돌려도 검증력이 생기지 않는다.

### 단계 B — 한 가수의 제작부터 곡 export까지 실제로 완결

가장 높은 제품 효과를 가진 다음 기능 묶음이다. 승인된 범위를 줄이지 않고, 먼저 제한된 일본어 corpus로 개발 경로를 닫는다.

1. 실제 녹음 또는 직접 설계한 recipe의 원본과 revision을 고정한다.
2. take 생성/수집 → marker/pitch/F0 편집 → 재생 검토 → 선택한 revision 확정.
3. template이 아닌 실제 unit bank와 manifest를 출력한다.
4. 새 설치 위치에 설치하고 source 작업 폴더 없이 정확한 bank identity를 해결한다.
5. 제작에 사용하지 않은 phrase를 preview하고, 수동 수정·undo·save/reopen·final WAV export를 수행한다.
6. source가 바뀌어도 이전 bank/프로젝트의 소리가 의도치 않게 바뀌지 않는지 확인한다.
7. 실제 오디오를 비교해 발음·음정·연결·음색 문제를 기록한다.

여기서 만들어진 결과는 “완성된 개발용 한 경로”다. 일본어 하나를 끝냈다고 Full-Scope Beta GO로 승격하지 않는다.

### 단계 C — 실제 가창의 음소·표현 범위를 넓힌다

B에서 발견한 실제 문제를 기반으로 U16/U17/U19/U20/U39를 수행한다. procedural 유성 자음·비음·파열과 문맥 연결, voiced attack/release, acoustic join, paired style blend, 시간 변화하는 timbral expression이 대상이다.

완료 조건은 control 존재 여부가 아니라 paired audio, 구간별 측정, 표현 변경의 재현, clipping/불안정 부재, 수동 편집 보존이다. 수치로 검증하기 어려운 자연스러움은 독립 청취 자료로 남긴다.

### 단계 D — 신경망 데이터·실제 모델 경로를 병행한다

A/B와 독립적으로 가능한 source/dataset 계약과 pilot 학습을 진행한다. U35는 CLAP 작업에 묻히지 않는 별도 책임 단위여야 한다.

모델을 학습했다고 끝나지 않는다. 실제 CPU helper와 일반 preview/export 연결, 악보 조건 반영, 표현 ownership, 취소/실패/잘못된 model package가 모두 필요하다. 시험 모델과 qualified singer를 구분한다.

### 단계 E — 같은 완결 경로를 영어·한국어와 native UX에 확장한다

언어별 dictionary/규칙뿐 아니라 해당 음소와 문맥을 실제로 발성하는 resource가 필요하다. sample/procedural/neural 사이의 overlap·cache·timeline 동작을 맞추고, USTX/SMF 변환은 손실 review부터 저장·재열기까지 사용자 경로로 검증한다.

사용자가 지적한 노트 겹침, 긴 텍스트 overflow, character 활용은 최소 창 크기·다국어·IME·zoom·selected/playing/error 상태의 실제 화면으로 확인한다. 모델 테스트만으로 디자인을 완료 처리하지 않는다.

### 단계 F — 자원과 기준 고정 후 exact installed release를 검증한다

변경을 reviewable commit으로 통합하고 clean checkout에서 재현 가능한 후보를 만든다. 해당 후보의 자원·모델·character·helper identity를 고정한 후 macOS/Windows, CLAP/VST3/AUv2 및 요구된 9개 host tuple을 검사한다.

official validator, 실제 plugin full soak, 설치·save/reopen·offline bounce, 독립 음악/언어/creator 자료를 exact artifact에 묶고 restored release audit을 수행한다. gate를 통과하기 위해 missing evidence를 fabricated PASS로 바꾸지 않는다.

### 동시에 유지할 작업 축은 세 개로 충분하다

- 음악 엔진·언어 정확성: A, C의 핵심 DSP·편집 보호.
- Voice Designer·producer·자원/모델: B와 D의 실물 결과.
- host·native UX·증적 통합: 실제 binary 실행, E/F와 shared-file 통합.

모델/녹음/독립 평가에는 별도 담당 역할과 산출물 일정이 필요하다. 다만 외부 판단이 없다는 이유로 내부 publication·renderer 코드를 중단할 이유는 없다. 공유 CMake·domain/schema·native controller는 직렬 통합한다.

## 12. Full-Scope 요구사항을 현재 코드에 다시 대응시키면

아래는 완료 체크가 아니라, 이번 리뷰로 확인한 기반과 아직 남은 출구의 대응표다.

| 요구사항 | 확인된 기반 | 아직 필요한 핵심 출구 |
|---|---|---|
| R1 timing·melody | 공유 compiler·ordered timing·sample voice 분리 | 전체 발음/voiced 구간 품질 및 backend parity |
| R2 expressions | 저장·기본 lane·ownership | 고급 lane의 실제 DSP와 청취/측정 |
| R3 original Voice Designer | recipe·발성·성도·native audition | 일반 가사·여성 가수 음색과 재사용 경로 |
| R4 real/generated sources | 수집·생성·batch·revision | 두 source route의 완전한 실제 제작 결과 |
| R5 reproducible bank | repository·불변 take·설치 기반 | reviewed take에서 실제 package로 publication |
| R6 range/style/coverage | inventory·style selection·coverage 검사 | 실물 range/style 및 paired blending |
| R7 Japanese/English/Korean | 세 언어 resolver | context 결함 복구·사전·자원·native 검토 |
| R8 classical renderer | Raw/PSOLA/Spectral/Stretch 연결 | 전체 voiced 구간 및 진실한 capability/provenance |
| R9 neural singer | model contract·bounded worker protocol | dataset·model·vocoder·native pipeline·CPU 검증 |
| R10 auto performance | proposal/accept·take/harmony 기반 | 모델·고급 표현·완전한 creator workflow |
| R11 native editor | 편집 모델·semantic/accessibility 기반 | 실제 긴 텍스트·IME·작은 창·사용성 검증 |
| R12 score interchange | bounded USTX/SMF와 변환 서비스 | 외부 파일·손실 설명·native 왕복 검증 |
| R13 hosts | CLAP adapter·offline readiness 기반 | 현재 결함 수정·실제 installed 9개 tuple |
| R14 character | 상태별 asset binding | phoneme 동기 performance·실제 자산 검증 |
| R15 safety/recovery | revision/cancellation/journal 기반 | 공통 입구 제한·callback 경계·실플랫폼 재검증 |
| R16 quality evidence | corpus/criterion/validator 정의 | 실제 곡·독립 청취·creator raw evidence |
| R17 exact release | 기존 배포·support 구조 | 현재 후보의 clean build·signed installed 증적 |
| R18 full-product gate | fail-closed semantic verifier | 완결된 실제 report와 restored audit |
| R19 provenance/rights | source identity·승인 필드 | 실제 배포할 모든 자원의 적용 가능한 증빙 |
| R20 connected lifecycle | 여러 구간의 UI·CLI 연결 | source-to-bank-to-song 전체 과정의 완결 |

## 13. 검증으로 답해야 할 남은 질문과 최종 권고

현재 소스만으로 답할 수 없는 중요한 질문은 다음과 같다.

- 제한된 실제 음성 자료에서 voiced attack/release 오류가 얼마나 들리며, 어떤 backend가 목표를 만족하는가?
- 직접 설계한 음색을 일반 가사로 확장했을 때 여성 음색·명료도·개성이 함께 유지되는가?
- source 작업 폴더가 없는 새 설치에서 동일한 프로젝트를 복원할 수 있는가?
- 실제 CPU model pilot의 품질·메모리·지연이 승인한 사용자 경험을 만족할 가능성이 있는가?
- 요구된 host와 native 화면에서 데이터 보존·오디오·가독성이 동시에 성립하는가?

이 질문들은 사용자에게 다시 승인받기 위한 중단 사유가 아니라, 다음 구현과 평가의 출구 조건이다.

**최종 권고는 “지금의 구조를 버리지 말고, 기능을 넓히는 속도를 줄여 실제 제작·노래 경로를 먼저 완결하라”다.** 현재까지의 작업은 헛수고가 아니다. 실제 compiler·DSP·editor·producer 기반을 쌓았다. 그러나 그 기반을 완성된 제품으로 환산하는 과정이 늦어졌고, 일부 테스트와 보고가 그 간극을 가렸다.

이번 보고서의 핵심 정정은 분명하다. **현재는 출시 직전의 95% 제품이 아니라, 넓은 기반 위에 중요한 내부 연결과 실제 자원이 아직 빠져 있는 개발 상태다.** 다음 진척은 파일 수나 PASS 수보다 “새 목소리 하나로 처음 만든 곡을 끝내고 다시 열 수 있는가”로 보여 주는 것이 타당하다.
