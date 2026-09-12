# 현재 통합 후 개발 방향 감사 — 증거와 재현

기준: 2026-09-09 한국시간, 현재 dirty worktree. 기존 두 감사 문서는 보존했다. 제품 소스·기존 테스트·계획·Git index·출시 상태는 수정하지 않았다.

주 산출물은 `report.html`, 편집 가능한 원문은 프로젝트 루트의 `DEVELOPMENT_DIRECTION_POST_INTEGRATION_REVIEW_2026-09-09_KO.md`다. HTML은 같은 원문의 canonical artifact를 packaged reader로 만든 단일 보고서다. 별도 대시보드나 공개 배포가 아니다.

## 보고서 설계

- 대상: 기술 검토자와 프로젝트 소유자.
- 질문: 현재 개발 방향을 유지해야 하는가, 다음 큰 개발 묶음을 어디에 집중할 것인가.
- 기준: 승인된 Full-Scope 계획과 현재 source/runtime. 이전 감사의 test count와 이미 수정된 결함은 현재값으로 재사용하지 않는다.
- Technical summary: 1장. 핵심 근거: 3–12장. 범위·정의: 2–3장. 방법: 실제 source/caller/test/contract 대조 및 별도 반례, 3/6/7/8/11/12장에 결과와 함께 배치. 한계: 각 해당 문단 및 16장. 권고: 14–15장. 추가 검증 질문: 16장.
- 시각화 계약: 주된 증거는 정확한 검증/이전-현재/요구 traceability 표다. HTML의 한 수평 막대 차트는 실제 로그에 나온 네 C++ 실패 case의 중복 실행 횟수(3,2,2,2)를 비교한다. 6장 끝 설명과 인접하게 둔다. 0 기준의 횟수 축, 단일 계열·범례 없음, 중립 단일 팔레트 및 직접 category label이다. 데이터는 4개 원인, 동일한 한 번의 전체 실행, log occurrence 단위이며 source closure는 제외한다. 분모가 다른 CTest와 core를 같은 차트에 섞지 않는다. 추세 데이터와 전체 제품 완료율은 근거가 없어 차트화하지 않는다. 빌더의 필수 chart 조건을 이 유용한 중복 집계 도표로 충족하며 별도 dashboard를 만들지 않는다.
- Delivery mode: Codex runtime의 portable HTML. 외부 업로드·Sites publish 없음. render/overflow QA는 공식 portable builder에 맡긴다.
- 한국어 제목과 본문은 사용자의 이번 언어 지시를 따른다.

## 보존 파일

- `results.json`: source/contract/library hash, 최신 CTest 분모·실패, 독립 반례와 미검증 경계.
- `ctest-full.log`, `ctest-failures.txt`: 03:02–03:06 KST 전체 실행 원본. 중복 suite 실패가 포함된다.
- `matrix.json`: 현재 실제 CLAP binary engineering matrix. releaseEligible=false.
- `recovery_review_probe.cpp`: control vs torn journal 이후 정상 save/import/verify/review. 현재 결함을 재현하면 exit 0이며, 제품 PASS라는 뜻이 아니다.
- `language_path_probe.cpp`: 현재 EnglishPhonemizer의 sing 역할 및 선택적 canonical path 비교.
- `gate_subpredicate_probe.py`: README를 한 operation 증거로 넣는 좁은 validator 반례. 완전한 report/GO 우회가 아니다.
- `artifact.json`, `assemble_report.mjs`: 보고서 canonical 입력과 증거 추출/패키징 준비. 제품 소스 변경 시 자동 재라벨링하지 않도록 tracked diff hash를 검사한다.

## 주요 실행

프로젝트 루트에서 전체 검증은 `cmake --build build/release -j 4`, `ctest --test-dir build/release --output-on-failure -j 1`이다. 이번 빌드는 up-to-date였으며 clean checkout 재빌드가 아니다. 로그는 보고서 산출물 작성 전 실행한 결과다.

좁은 Python 진단: `PYTHONPATH=. python3 -B docs/reviews/direction-post-integration-2026-09-09/gate_subpredicate_probe.py`.

복구 C++ 진단은 현재 Release static libraries에 링크하고, 새 `mktemp -d` 디렉터리를 인자로 사용한다. 기존 fixture를 재사용하거나 덮어쓰지 않는다. 필요한 include는 `tests`, `build/release/generated`, core/domain/formats/text/phonemizer/voicebank/voicebank-production/voice-design/synthesis이며 `cmake/AppleClangStopTokenCompatibility.hpp`를 force-include한다. 링크 순서는 voicebank_production, voicebank, formats, core, voice_design, text, synthesis, voicebank, formats, phonemizer, domain, core다. 진단은 기존 CMake suite를 재빌드하지 않는다.

보고서 준비: `node docs/reviews/direction-post-integration-2026-09-09/assemble_report.mjs`. 이후 Data Analytics build-report의 공식 `deliver_portable_artifact.mjs --input .../artifact.json --output .../report.html`을 사용한다. HTML은 생성물이며 직접 수정하지 않는다.

## 검증 해석

최신 전체 결과는 110/116이고 core는 769/773이다. 네 C++ 실패 case가 여러 suite에서 중복된다. 다른 하나의 실패 원인은 미색인 필수 입력 311개다. 제품 완료율과 구분한다. 이 폴더와 새 보고서는 해당 숫자 측정 이후 추가됐다.

독립 reviewer는 세 명의 제한된 기술 검토 작업을 의미한다. 실제 음악가·원어민·최종 release reviewer의 수락을 받았다는 의미가 아니다. 모든 새 음성·권한 fixture는 합성 engineering 자료다.
