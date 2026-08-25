# Project SEAM 사용 가능 Alpha 승인 계약

**정식 기준 문서:** 영문 [`USABLE_ALPHA_ACCEPTANCE.md`](USABLE_ALPHA_ACCEPTANCE.md)가 기계 판정과 제품 승격의 canonical gate다.

**현재 결과:** `BLOCKED`

Project SEAM은 한 사람이 Apple Silicon Mac에서 CLI와 DAW 없이 아래 모든 필수 작업을 끝냈을 때만 **Usable Alpha**로 승격한다. Phase 구현, CI 구성, 플랫폼 소스, 배포 Gate, 모듈 테스트 통과는 이 사용자 수직 흐름을 대신하지 않는다.

## 현재의 정확한 제한

네이티브 Standalone은 이제 `AuthoringSession`과 공유 `ProductionProjectRenderer`로 프로젝트 소유 Note, Voicebank 해석, Transport, backing media, export를 처리한다. 결정론적 테스트 모드는 threaded fallback audio와 development fixture를 사용할 수 있지만, 이는 실제 장치 acceptance가 아니다. 권리 정리된 production bank, 대상 Apple Silicon runtime 증적, reopen/recovery/export 청취 증적, 실제 곡 안정성 세션이 UI를 통해 기록될 때까지 계약은 blocked 상태다.

Phase 12C·13A·13B는 합성 검증, 배포, 인증, 콘텐츠/IP Gate를 제공하지만 Standalone 실사용성을 증명하지 않는다.

## 필수 승인 항목

- [ ] **UA-001 — 실행:** Finder에서 `Project SEAM.app`을 실행해 반응 가능한 편집기에 진입한다.
- [ ] **UA-002 — 새 프로젝트:** 이름, Tempo, Sample Rate, Output Channel, 정확한 Voicebank를 선택해 새 프로젝트를 만든다.
- [ ] **UA-003 — 저작 구조:** Vocal Track과 Vocal Region을 최소 하나씩 추가한다.
- [ ] **UA-004 — 음악 입력:** 30초 이상의 Note와 일본어 가사를 입력한다.
- [ ] **UA-005 — 세부 정보:** 생성된 Phoneme과 선택된 Source Unit을 화면에서 확인한다.
- [ ] **UA-006 — Phoneme 편집:** Phoneme Boundary를 이동하고 Override가 저장되는지 확인한다.
- [ ] **UA-007 — Unit 편집:** 다른 Unit Variant 또는 Renderer를 선택하고 음성 변화를 확인한다.
- [ ] **UA-008 — Pitch 편집:** Pitch Point를 추가·이동·삭제한다.
- [ ] **UA-009 — Seam 편집:** 선택한 경계의 Seam Parameter를 변경한다.
- [ ] **UA-010 — 생산 음성:** 데모 Oscillator가 아니라 보이는 Project에 대응하는 production sample-concatenative 음성을 듣는다.
- [ ] **UA-011 — Transport:** 과거 편집의 stale audio 없이 Play·Pause·Stop·Seek·Loop를 수행한다.
- [ ] **UA-012 — 저장:** 사용자가 선택한 경로에 프로젝트를 저장한다.
- [ ] **UA-013 — 재열기:** 앱을 종료한 뒤 저장된 프로젝트를 다시 연다.
- [ ] **UA-014 — 음성 동등성:** 같은 프로젝트와 정확한 Voicebank로 materially identical한 음성을 듣는다.
- [ ] **UA-015 — 복구:** 강제 종료 후 Autosave로 Dirty Project를 복구한다.
- [ ] **UA-016 — Voicebank Relink:** Voicebank 누락을 감지하고 자동 대체 없이 정확한 ID·Version·Content Hash로 Relink한다.
- [ ] **UA-017 — Master 출력:** Final Quality Master WAV를 출력한다.
- [ ] **UA-018 — Stem 출력:** Vocal Stem WAV를 최소 하나 출력한다.
- [ ] **UA-019 — 외부 확인:** 외부 플레이어에서 WAV의 길이·채널·가청 콘텐츠를 확인한다.
- [ ] **UA-020 — 안정성:** 30분 동안 Audio Underrun·Data Loss·무제한 Memory Growth 없이 작업한다.

## 정량 기준

- Target M3 Max에서 Voicebank Index가 준비된 상태의 Cold Launch가 3초 미만이어야 한다.
- 48 kHz Preview에서 2초 Phrase 편집 후 소리가 들리기까지 median 150 ms 미만, p95 400 ms 미만이어야 한다.
- 10,000 Note Piano Roll은 60 FPS를 목표로 하며 일반 선택·Pan·Zoom Frame은 50 ms를 넘지 않아야 한다.
- 계측 빌드의 Audio Callback Allocation과 Lock은 0이어야 한다.
- 48 kHz/128 frames의 30분 세션에서 Underflow Frame은 0이어야 한다.
- 5분·4 Track Project 저장은 Backing Media 복사 시간을 제외하고 1초 미만이어야 한다.
- Autosave로 인한 UI 정지는 50 ms 미만이어야 하며 Serialization과 Durable Write는 UI Thread 밖에서 실행해야 한다.
- Export는 진행률·취소·Atomic Publication을 제공하고 부분 파일을 요청된 최종 경로에 남기면 안 된다.
- Cache Warm-up 이후 30분 세션의 단조 Memory 증가는 Baseline 대비 100 MiB 미만이어야 한다.

## 증적 정책

기계 판정 파일은 [`usable-alpha-acceptance.json`](usable-alpha-acceptance.json)이다. `PASS`에는 Repository 내부의 안전한 Evidence 경로와 실제 SHA-256이 필요하다. 20개 Mandatory 항목이 모두 `PASS`일 때만 Gate를 `PASSED`로 바꿀 수 있다.
