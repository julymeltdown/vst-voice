# 개발 방향 재검토 자료 — 2026-09-09

현재 코드 감사의 새 스냅샷이다. 앞선 direction-audit-2026-09-09 폴더를 덮어쓰지 않는다.
제품 코드를 수정하지 않았고, 보고서·재현용 진단·생성 HTML만 추가했다.

## 보고서 구성과 근거

- 주 보고서: report.html. 같은 전체 본문의 편집 가능한 source는 루트 DEVELOPMENT_DIRECTION_REVIEW_REFRESH_2026-09-09_KO.md.
- artifact.json: canonical portable report payload. results.json: 실제 실행 요약과 identity.
- audience: technical. delivery mode: portable HTML in Codex desktop runtime; 공개 게시 없음.
- build-report specification mapping: title→첫 블록, technical summary→1절, definitions/scope/method→2절, key evidence→3–11절, limitations/robustness→각 발견 및 14절, next steps→12–13절, further questions→14절.
- 정의를 숫자보다 먼저 읽도록 scope/metric 정의를 key findings 앞에 배치했다.
- visualize-data에 따라 완료율·시계열 추정 차트는 생략했다. 실제 effort/quality 시계열이 없고 테스트·로드맵·제품은 서로 다른 분모다. 3절 실행표, 4절 이전/현재 대조표, 11절 R1–R20 대응표는 정확한 대조용이다. 모든 표는 단일 열 문서 흐름, 중립 색, 표 앞뒤 설명을 가진다.
- portable report 계약이 chart block을 요구하므로 targeted correction으로 2절 뒤에 변경 경로 분포를 추가했다. 두 번째 validation에서는 실제 SQL provenance를 요구하여 같은 원본 Git path를 SQLite JSON1으로 재집계하고 그 쿼리를 보존했다. 기존 본문·표·근거는 삭제하거나 축약하지 않았다.
- chart contract: 질문=현재 통합 검토 범위가 어느 디렉터리에 분산되어 있는가; takeaway=소스·테스트·문서가 함께 바뀌어 source checkpoint가 필요하다; family=comparison/bar-horizontal; 9개 영역을 465개 원본 Git path에서 집계; 긴 디렉터리 label과 직접 축으로 구분; shared reader의 단일 quantitative series palette와 중립 scaffold; full-width HTML; 완료율·작업량 해석 금지.
- snapshot.status=ready는 감사 보고서 완성을 뜻한다. 제품은 명시적으로 NO-GO다.
- 원본 전체 CTest 로그는 build/release/Testing/Temporary/LastTest.log이며 이후 테스트가 덮어쓸 수 있다. 이 감사의 명시적 결과는 results.json에 유지한다.
- 결과 수는 겹치는 suite를 합산하지 않았다. Git/source-closure 수는 보고서 생성 전 snapshot이다.
- 실제 GUI·DAW·Windows·음악/모델/권리 qualification은 이 실행에서 수행하지 않았다.

## 제품 검증 재실행

프로젝트 루트에서 다음 명령을 사용한다. 새 실행은 결과가 달라질 수 있다.

```sh
cmake --build build/release -j 4
ctest --test-dir build/release --output-on-failure -j 1
PYTHONPATH=. python3 docs/reviews/direction-audit-refresh-2026-09-09/gate_probe.py
```

gate_probe.py는 메모리 내 synthetic 객체만 사용하며 실제 상태·candidate 파일을 읽거나 쓰지 않는다.
ownership_probe.cpp는 반드시 새 임시 폴더를 인자로 받아야 한다. 그 폴더에만 synthetic WAV와 producer workspace를 생성한다.
probe exit 0은 진단 실행 성공이며 결함이 없다는 뜻이 아니다.

## C++ 진단 빌드

프로젝트 루트에서 아래의 컴파일 명령을 사용할 수 있다. 실행 파일과 workspace 출력은 새 임시 디렉터리로 지정한다.

```sh
mkdir -p /tmp/seam-review-diagnostic-build
c++ -std=c++20 -O2 -Itests -Ilibs/seam-core/include -Ilibs/seam-domain/include -Ilibs/seam-time/include -Ilibs/seam-formats/include -Ilibs/seam-synthesis/include -Ilibs/seam-phonemizer/include -Ilibs/seam-voicebank/include -Ilibs/seam-voicebank-production/include -Ilibs/seam-voice-design/include docs/reviews/direction-audit-refresh-2026-09-09/ownership_probe.cpp build/release/libseam_voicebank_production.a build/release/libseam_voice_design.a build/release/libseam_synthesis.a build/release/libseam_voicebank.a build/release/libseam_formats.a build/release/libseam_phonemizer.a build/release/libseam_text.a build/release/libseam_domain.a build/release/libseam_core.a -o /tmp/seam-review-diagnostic-build/ownership_probe
c++ -std=c++20 -O2 -Ilibs/seam-core/include -Ilibs/seam-domain/include -Ilibs/seam-time/include -Ilibs/seam-formats/include -Ilibs/seam-synthesis/include -Ilibs/seam-phonemizer/include -Ilibs/seam-application/include -Ilibs/seam-voicebank/include docs/reviews/direction-audit-refresh-2026-09-09/pronunciation_probe.cpp build/release/libseam_application.a build/release/libseam_phonemizer.a build/release/libseam_synthesis.a build/release/libseam_voicebank.a build/release/libseam_formats.a build/release/libseam_text.a build/release/libseam_domain.a build/release/libseam_core.a -o /tmp/seam-review-diagnostic-build/pronunciation_probe
```

현재 라이브러리에 연결하는 명령이며 clean commit 재현 증거는 아니다. ownership 진단의 최초 임시 빌드는 include 경로 하나 누락으로 실패했고, 기존 phonemizer include 경로를 추가한 뒤 성공했다. 제품 코드는 바꾸지 않았다.

## 보고서 패키징

artifact.json은 루트 Markdown의 각 독립 ## 절을 각각 markdown block으로 보존한 payload다.
build-report plugin의 report:deliver가 동일 payload 검증·공유 reader·semantic fallback·브라우저 확인을 수행한다.
report.html은 생성 파일이다. delivery receipt의 verification 상태가 브라우저 검사 완료 여부를 구분한다.

최종 delivery.json은 validation/package/verification 모두 passed다. 설치된 Chromium에서 1440px/390px의 reader·overflow·source dialog 확인이 통과했다. 이 결과는 보고서 화면 검사이며 Project SEAM 제품 GUI 검사가 아니다. Markdown 원본 15개 블록은 모두 보존하고 chart 설명/표현 2개 블록만 추가했다.
