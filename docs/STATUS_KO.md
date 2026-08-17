# Project SEAM 현재 개발 상태

**기준일:** 2026-08-17  
**브랜치 정책:** `master` 단일 브랜치  
**현재 제품 단계:** **G1 Feature Alpha**

Project SEAM은 샘플 연결형 합성 코어, 네 개의 Renderer, 네이티브
Standalone 기반, 서명된 `.seambank`, CLAP GUI와 **생산 합성 Preview
통합**까지 구현했다. 아직 전체 제품 기능과 상용 인증이 끝난 Release
Candidate는 아니다.

## 현재 완료 수준

| 영역 | 상태 | 정확한 의미 |
|---|---|---|
| 악보·음소·Unit·Seam 도메인 | 구현됨 | canonical project와 undoable command가 존재한다. |
| 합성 엔진 | 구현됨 | Raw, PSOLA, SpectralClassic, Stretch, Timing, Seam, Phrase Cache가 존재한다. |
| CLAP Production Preview | **Phase 12A 완료** | CLAP 비동기 Preview가 Standalone과 같은 생산 PhraseRenderPipeline을 사용한다. |
| Voicebank Resolution | **Phase 12A 완료** | ID/version/content hash, 설치 receipt, trust, relink/select API, no-silent-fallback을 지원한다. |
| Embedded technical lanes | 부분 구현 | 표시와 Seam 편집은 가능하나 Phoneme/Unit/Pitch 전체 직접 편집은 남아 있다. |
| Host timeline·routing | 부분 구현 | 첫 Track/Region·Stereo 중심이며 Host tempo/loop와 1–8채널 통합이 남아 있다. |
| Linux/X11 | Feature Alpha | 실제 창·오디오·CLAP child GUI 경로가 실행된다. |
| Windows/macOS | Source Ready | 대상 OS 실제 런타임·IME·오디오 인증이 남아 있다. |
| VST3/AU | Source Ready | wrapper 계약만 있으며 target binary/validator 증적이 없다. |
| Character 01 | 통합됨·미확정 | 런타임 자산은 있으나 이름·최종 3D·상업 권리 확정이 남아 있다. |
| Official Voicebank 01 | 미완료 | 공개 도메인 Fixture는 계약 기반 정식 상품이 아니다. |

## Phase 12A의 정확한 변화

기존 단일 모음 전용 Preview를 제거하고 다음 경로를 공유한다.

```text
Project Snapshot
→ 정확한 Voicebank resolve
→ PhraseSegmenter
→ Phonemizer
→ Unit Selector
→ Timing Solver
→ Raw / PSOLA / Spectral / Stretch
→ SeamComposer
→ content-addressed PCM Cache
→ bounded CLAP publication
```

저장된 Voicebank와 ID, version 또는 content hash가 다르면 Preview는
명시적으로 실패하며 무음을 게시한다. 다른 Bank로 자동 대체하지 않는다.
Project/SEAMED11 state는 정확한 참조를 보존하며, refresh·relink root·exact
selection API로 복구할 수 있다.

## 현재 출시 판단

가능:

- 엔진·UX 연구와 내부 데모
- Linux/X11 Feature Alpha
- 실제 생산 Pipeline을 사용하는 CLAP Preview 검증
- Voicebank 설치·변조·누락 복구 시나리오 검증

아직 불가:

- G2 Feature Complete 또는 상용 RC 선언
- Windows/macOS 지원 완료 표기
- VST3/AU·상용 DAW 인증 완료 표기
- Official Voicebank 01 판매
- signed/notarized installer 완료 표기

상세 잔여 작업은 `REMAINING_TASKS_KO.md`, 출시 Gate는
`RELEASE_READINESS_KO.md`를 따른다.
