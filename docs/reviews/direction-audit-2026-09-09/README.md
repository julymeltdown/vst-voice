# 재현·검토 메모

이 폴더는 코드 변경이 아니라 2026-09-09 개발 방향 보고서의 근거 자료다.

- 주 보고서: 프로젝트 루트 DEVELOPMENT_DIRECTION_DEEP_REVIEW_2026-09-09_KO.md.
- 읽기용 산출물: report.html. 한 개의 portable HTML 보고서이며 공개 사이트에는 게시하지 않았다.
- 원본 표현: artifact.json. 주 Markdown의 13개 본문 section을 보존하고 Git 통합 범위 그림 하나를 추가했다.
- 실행 관측: results.json. 재현 성공의 exit 0과 제품 요구사항 PASS를 구별한다.
- 언어 반례: language_probe.cpp. 파일을 읽거나 프로젝트에 저장하지 않고 메모리 내 프로젝트로 공개 라이브러리를 호출한다.
- Git 원자료: git-paths.json. 이 리뷰가 새로 만든 파일만 제외하고 당시 446개 경로를 보존했다. 기존 리뷰/원장은 이미 변경 대상이었으므로 제외하지 않았다.

## 분석 계약

독자는 프로젝트 소유자와 C++ 구현 담당자다. 의사결정은 구조 유지 여부, 다음 개발 우선순위, 기존 진척 해석의 유효성이다. 기술 보고서 형식을 사용했다.

기술 요약은 1절, 주요 발견은 4–9절, 범위/분모/방법은 2–3절, 운영 근거는 10절, 다음 단계는 11절, 전체 요구 대응은 12절, 불확실성과 검증 질문은 13절이다. 각 finding 옆에서 runtime/source/historical 한계를 설명한다. 방법·분모를 앞에 배치해 PASS 및 완료율 혼동을 방지했다.

시각화는 실제 코드 리뷰 범위를 보여 주는 9개 root category의 bar 하나다. 음질 점수나 제품 완료율은 측정하지 않았으므로 해당 그래프를 만들지 않았다. 단일 작업 트리의 동일 지표이므로 중립적 단일계열 스타일과 디렉터리 직접 라벨을 사용하고 별도 범례는 두지 않는다. snapshot의 ready는 보고서가 준비됐다는 뜻이며 제품 Beta GO가 아니다.

구현 상태/R1–R20 대응표는 정밀 조회가 목적이므로 표를 사용했다. Git 경로 수는 코드량·시간·기능 완료량으로 해석하지 않는다. 원자료에는 status/path를 보존했지만 최종 chart에는 집계값만 제공한다.

## 언어 반례 재현

프로젝트 루트에서 실행한다. 기존 Release 라이브러리를 사용하므로 다른 코드가 변경되면 먼저 관련 타깃을 재빌드하고 빌드 identity를 기록한다.

~~~sh
audit_dir=$(mktemp -d /tmp/seam-direction-recheck.XXXXXX)
c++ -std=c++20 -O2 -arch arm64 \
  -include cmake/AppleClangStopTokenCompatibility.hpp \
  -Ilibs/seam-application/include -Ilibs/seam-phonemizer/include \
  -Ilibs/seam-domain/include -Ilibs/seam-core/include \
  docs/reviews/direction-audit-2026-09-09/language_probe.cpp \
  build/release/libseam_application.a build/release/libseam_phonemizer.a \
  build/release/libseam_domain.a build/release/libseam_core.a \
  -o "$audit_dir/language_probe"
"$audit_dir/language_probe"
~~~

예상되는 현재 결함 관측은 results.json의 languageProbe.stdout이다. 향후 수정 후에는 invalid_context_applied=0, orphan_warning=1, 언어 전환 unresolved=1이 되어야 한다. 이 진단은 테스트 프레임워크의 acceptance test가 아니라 반례 재현 프로그램이다.

## CLAP 실행 경계 반례

기존 matrix의 --plugin 인자가 실제 binary load를 요구하는지 확인했다. 최초 실행은 임시 폴더의 평문 파일을 plugin 인자로 사용했고, matrix stdout과 JSON이 모두 PASS였다. 같은 반례는 이 폴더의 일반 텍스트 파일 README.md를 plugin 인자로 전달해도 평가할 수 있다. 출력은 새 임시 경로로 지정한다.

~~~sh
audit_dir=$(mktemp -d /tmp/seam-clap-negative.XXXXXX)
build/release/phase12c/seam_phase12c_matrix "$audit_dir/matrix.json" \
  --plugin docs/reviews/direction-audit-2026-09-09/README.md \
  --bank assets/demo-human-voicebank-public-domain/production-bank
~~~

위 README.md 입력으로 이번에 다시 실행했다고 주장하지 않는다. 최초로 실제 사용한 입력은 결과 파일에 명시한 not-a-plugin.clap 평문이다. 정상적인 실제 binary host로 교체하면 이러한 입력은 오디오 처리 전에 실패해야 한다.

강화된 검증기로 기존 결과를 평가하는 정확한 명령은 results.json에 저장했다. 검증기의 BLOCKED는 증거 거부 상태이지 이 리뷰 작업 중단을 뜻하지 않는다.

## 보고서 QA와 독립 확인

원본 artifact를 제공된 portable builder로 검증·패키징한다. 수작업 HTML/chart runtime은 만들지 않았다. 패키저는 설치된 Chromium으로 1440px/390px, light/dark 렌더링, source interaction, payload 일치와 기본 overflow를 검사한다. 결과 receipt를 delivery.json에 보존한다.

독립 읽기 검토는 F05–08, F12–16의 근거·과장을 확인했다. 모든 backend에 모든 표현 지원을 요구하는 것으로 읽힐 수 있는 문장 하나를 수정했다. 현재 문장은 각 필수 표현에 검증된 resource/backend 조합이 하나 이상 있어야 한다는 승인 범위를 유지한다.

현재 native 앱/DAW의 새 시각 QA, 독립 청취, 전체 최신 CTest, 실제 CLAP matrix, 2시간 full soak를 수행한 것은 아니다. 기존 코드 변경은 보존했으며 리뷰 단계에서는 추가 제품 구현·commit·push를 수행하지 않았다.

