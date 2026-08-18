# Project SEAM 현재 개발 상태

**기준일:** 2026-08-18
**브랜치 정책:** `master` 단일 브랜치
**현재 제품 단계:** **G1 Feature Alpha**

Phase 12C의 Live Voicebank Articulation과 Linux 검증 도구가 구현됐고, Phase 13A의 VST3/AUv2·서명·Installer·Host 인증 파이프라인은 `SOURCE_READY / CI_CONFIGURED` 상태다. 제품 Gate는 실제 외부 검증이 남아 있으므로 G1에 유지한다.

## 구현 상태

| 영역 | 상태 | 정확한 의미 |
|---|---|---|
| 악보·음소·Unit·Seam 도메인 | 구현됨 | canonical Project와 undoable command가 존재한다. |
| 합성 엔진 | 구현됨 | Raw, PSOLA, SpectralClassic, Stretch, Timing, Seam, Phrase Cache가 존재한다. |
| Production CLAP Preview | 구현됨 | Standalone과 같은 생산 합성 및 1~8채널 Project Routing을 사용한다. |
| Live Voicebank Articulation | 구현 스냅샷 | Attack/Transition/Sustain/Release, legato, Note Expression, MIDI 1, de-click voice stealing이 존재한다. |
| Linux 검증 Harness | 구현됨 | 336-case matrix, allocation probe, cancellation storm, GUI lifecycle, soak runner가 존재한다. |
| Phase 12C Acceptance | **BLOCKED** | 공식 CLAP validator와 정확한 7,200초 full soak 등이 실제 PASS가 아니다. |
| VST3 | **SOURCE_READY / CI_CONFIGURED** | exact SDK lock, wrapper build, target CI, validator runner가 존재한다. 실제 target result는 NOT_RUN이다. |
| AUv2 | **SOURCE_READY / CI_CONFIGURED** | macOS build와 `auval` workflow가 존재한다. 실제 result는 NOT_RUN이다. |
| Signing·Installer | **PIPELINE_READY / EXTERNAL_GATE** | NSIS(zlib), codesign, Authenticode, notarytool, PKG source는 있으나 실제 인증서 실행은 NOT_RUN이다. |
| Commercial DAW Matrix | recorder 구현·NOT_RUN | 실제 DAW 실행 증적이 없다. |
| Character 01 | 통합됨·미확정 | runtime asset은 있으나 최종 명칭·3D·상표/IP 확정이 남아 있다. |
| Official Voicebank 01 | 미완료 | 공개 도메인 기술 Fixture는 계약 기반 정식 상품이 아니다. |

## 현재 가능한 범위

- Linux/X11 Standalone과 CLAP Feature Alpha
- CLAP 내 직접 technical editing
- production sample-concatenation render와 multichannel routing
- live articulation 연구·내부 데모
- unsigned Linux developer package와 sandbox install/uninstall smoke
- target CI를 통한 VST3/AUv2 빌드·검증 요청

## 아직 출시 완료로 표시할 수 없는 범위

- Phase 12C G2 Acceptance
- Windows/macOS 실제 runtime PASS
- 실제 VST3 validator 및 `auval` PASS
- Windows Authenticode·Apple notarization PASS
- clean-OS installer PASS
- 실제 상용 DAW Matrix PASS
- Official Voicebank 01
- Beta·Release Candidate·General Availability

다음 개발 축은 **Phase 13B — Official Voicebank 01과 Character/IP Release Assets**다. 다만 Phase 12C/13A의 mandatory validation은 병렬로 실제 대상 환경에서 반드시 수행해야 한다.
