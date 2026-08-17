# Project SEAM 현재 개발 상태

**기준 커밋:** `b3d185144fba713d7ef209cd4ace17e303e46f73` 이후 문서 정리 커밋  
**기준일:** 2026-08-17  
**브랜치 정책:** `master` 단일 브랜치  
**현재 제품 단계:** **Feature Alpha**

Project SEAM은 샘플 연결형 가창합성의 핵심 도메인, 네 개의 렌더러, 네이티브 에디터 기반, 멀티채널 라우팅, 서명된 `.seambank`, CLAP 플러그인 수직 경로까지 구현했다. 그러나 상용 배포 가능한 Release Candidate는 아니다.

## 1. 현재 완료 수준

| 영역 | 상태 | 정확한 의미 |
|---|---|---|
| 악보·음소·Unit·Seam 도메인 | 구현됨 | Note, Lyric, Phoneme, Unit override, Seam, Pitch automation이 프로젝트 상태로 존재한다. |
| 합성 엔진 | 구현됨 | Raw, Classic PSOLA, SpectralClassic, Stretch와 Phrase render pipeline이 존재한다. |
| Standalone Linux/X11 | Feature Alpha | 실제 창, 입력, 오디오 출력, Voicebank Studio 수직 경로가 동작한다. |
| Windows/macOS 네이티브 어댑터 | Source Ready | 대상 OS 소스와 CI 계약은 있으나 현재 Linux 패키지에서 실제 장치·IME·호스트 인증을 수행하지 않았다. |
| `.seambank` | 구현됨 | Ed25519 서명, 검증, 신뢰 저장소, transactional install이 존재한다. |
| CLAP Render Player | 구현됨 | 사전 렌더된 PCM의 host transport 재생 경로다. |
| CLAP Embedded Editor | 부분 구현 | Linux/X11 child GUI, Note/Lyric/Seam 편집, 기술 Lane 표시, 비동기 Preview, live sample note input이 동작한다. |
| VST3/AU | Source Ready | wrapper 계약만 존재하며 실제 binary와 validator 증적은 없다. |
| Character 01 | 통합됨·미확정 | canonical runtime asset은 통합됐으나 정식 이름, 최종 3D 제작, 상업 권리 검토는 남아 있다. |
| Official Voicebank 01 | 미완료 | 공개 도메인 음성은 기술 fixture일 뿐이며 계약 기반 정식 보이스뱅크가 아니다. |

## 2. Phase 11 범위의 정확한 해석

Phase 11에서 실제 직접 편집 가능한 항목은 다음과 같다.

- Note 생성·선택·이동·삭제
- Unicode lyric 입력
- Seam amount 변경
- Undo/Redo
- Character 표시 모드 변경

다음은 화면에 표시되지만 아직 완전한 직접 조작 편집기로 완성되지 않았다.

- Phoneme boundary 이동
- Unit 후보/Variant/Renderer 선택
- Pitch automation point 추가·이동·삭제
- Unit별 Sample Microscope 호출과 marker 편집

또한 Phase 11의 CLAP 비동기 Preview는 현재 `libs/seam-clap-editor/src/editor_runtime.cpp` 안에서 공개 도메인 단일 인간 모음 샘플을 음표마다 피치 이동·루프하는 전용 경로다. Standalone에서 이미 구현된 전체 생산 합성 경로—Phonemizer → Unit Selector → Timing Solver → Raw/PSOLA/Spectral/Stretch → SeamComposer → Cache—를 아직 호출하지 않는다.

따라서 현재 플러그인은 **제품 구조를 검증한 Embedded Editor Feature Alpha**이며, 정식 Voicebank를 사용하는 완성형 DAW 가창합성기는 아니다.

## 3. 독립 재검증 결과

새로운 빈 빌드 디렉터리에서 다음을 확인했다.

```text
Warnings-as-errors Phase 11 target build  PASS
Phase 11 CTest                           3/3 PASS
Core named tests                        128 PASS / 0 FAIL
Master-only branch policy               PASS
Dependency/license audit                PASS
Phase 11 source/rights contracts        PASS
Git object integrity                    PASS (dangling blobs only)
```

`git fsck --full`의 dangling blob은 참조되지 않는 Git object이며 현재 브랜치의 파일 손상이나 commit 단절을 의미하지 않는다.

## 4. 출시 판단

현재 상태로 가능한 것:

- 엔진·UX 연구와 내부 데모
- Linux/X11 기술 프리뷰
- 샘플 연결 합성의 음향 실험
- 보이스뱅크 제작 도구의 내부 사용
- CLAP child GUI와 note-event 수직 경로 검증

현재 상태로 하면 안 되는 것:

- 상용 Release Candidate 선언
- Official Voicebank 01 판매
- Windows/macOS 지원 완료 표기
- VST3/AU 지원 완료 표기
- REAPER, Bitwig, Cubase, Ableton, Studio One, FL Studio, Logic 인증 완료 표기
- signed/notarized installer 제공을 완료했다고 표기

상세 잔여 작업은 [`REMAINING_TASKS_KO.md`](REMAINING_TASKS_KO.md), 출시 Gate는 [`RELEASE_READINESS_KO.md`](RELEASE_READINESS_KO.md)를 따른다.
