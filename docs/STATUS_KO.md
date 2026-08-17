# Project SEAM 현재 개발 상태

**기준일:** 2026-08-17  
**브랜치 정책:** `master` 단일 브랜치  
**현재 제품 단계:** **G1 Feature Alpha**

Phase 12B까지 구현됐다. CLAP Embedded Editor는 이제 직접 Phoneme·Unit·Pitch
편집을 수행하고, 전체 Project의 복수 Track·Region을 공유 생산 합성 경로로
렌더한 뒤 Phase 6 Bus/Matrix를 통해 1~8채널로 출력한다. Host seconds 또는
beats+tempo, loop, seek, project start offset과 offline render mode도 플러그인
재생 위치와 render quality에 반영된다.

## 현재 완료 수준

| 영역 | 상태 | 정확한 의미 |
|---|---|---|
| 악보·음소·Unit·Seam 도메인 | 구현됨 | canonical Project와 undoable command가 존재한다. |
| 합성 엔진 | 구현됨 | Raw, PSOLA, SpectralClassic, Stretch, Timing, Seam, Phrase Cache가 존재한다. |
| CLAP Production Preview | Phase 12A 완료 | Standalone과 같은 생산 Phrase pipeline을 사용한다. |
| Voicebank Resolution | Phase 12A 완료 | ID/version/content hash, receipt/trust, relink, no-silent-fallback을 지원한다. |
| Embedded technical editing | **Phase 12B 완료** | Phoneme boundary, Unit variant/renderer, Pitch CRUD/interpolation, Sample Microscope를 지원한다. |
| Host timeline | **Phase 12B 완료** | seconds 우선, beats+tempo fallback, loop/seek/start offset, time-signature metadata를 지원한다. |
| Project rendering/routing | **Phase 12B 완료** | 모든 audible Track/Region과 Phase 6 Bus/Matrix를 공유해 1~8채널을 생성한다. |
| CLAP port configuration | **Phase 12B 완료** | 1~8채널 config, port/config rescan, realtime/offline quality를 지원한다. |
| Live note input | 부분 구현 | 기술용 인간 모음 sampler가 있으며 실제 Voicebank transition/legato/expression은 남아 있다. |
| Linux/X11 | Feature Alpha | 실제 창·오디오·CLAP child GUI와 동적 Host 경로가 실행된다. |
| Windows/macOS | Source Ready | 대상 OS 실제 runtime·IME·audio/host 인증이 남아 있다. |
| VST3/AU | Source Ready | wrapper 계약만 있으며 target binary/validator 증적이 없다. |
| Character 01 | 통합됨·미확정 | runtime asset은 있으나 이름·최종 3D·상업 권리 확정이 남아 있다. |
| Official Voicebank 01 | 미완료 | 공개 도메인 Fixture는 계약 기반 정식 상품이 아니다. |

## Phase 12B의 정확한 변화

```text
Technical lane edit
→ canonical undoable Project state
→ immutable full-project render
→ exact Voicebank per track
→ every audible Track / Region
→ production Region renderer
→ Phase 6 routing buses and matrices
→ 1–8-channel bounded PCM publication
→ host timeline mapping
→ CLAP output
```

Project JSON은 schema 5이며 `hostStartOffsetTick`을 저장한다. Phrase-local
snapshot은 프로젝트 라우팅을 중복 적용하지 않으며, 최종 multichannel routing은
Project renderer에서 한 번만 수행된다.

## 현재 출시 판단

가능:

- 엔진·UX 연구와 내부 데모
- Linux/X11 Feature Alpha
- CLAP 내부 직접 기술 편집
- multi-track/multi-region/4채널 검증
- host timeline과 offline render contract 검증

아직 불가:

- G2 Feature Complete 또는 상용 RC 선언
- official `clap-validator` PASS 선언
- Windows/macOS 지원 완료 표기
- VST3/AU·상용 DAW 인증 완료 표기
- signed/notarized installer 완료 표기
- Official Voicebank 01 판매

다음 단계는 **Phase 12C — validator, target OS runtime, realtime/soak hardening**이다.
