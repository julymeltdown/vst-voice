# Project SEAM 출시 준비 Gate

현재 판정은 **G1 FEATURE_ALPHA**다. Phase 12A와 Phase 12B의 생산 Preview,
정확한 Voicebank Resolution, 직접 기술 편집, Host Timeline과 Multichannel
Project Routing은 완료됐지만 G2 전체 조건은 아직 충족하지 않는다.

## G2 Feature Complete Gate

- [x] CLAP가 production PhraseRenderPipeline 사용
- [x] `.seambank` ID/version/content hash resolve와 relink/select 기반
- [x] Phoneme/Unit/Pitch 직접 편집
- [x] Host tempo/loop/seek/start-offset 계약
- [x] Multi-track/region production render
- [x] 1–8채널 plugin routing
- [ ] Voicebank-driven live note articulation 범위 확정
- [x] Project technical state round trip

## G3 Beta Gate

- [ ] Linux/Windows/macOS runtime tests
- [ ] official CLAP validator PASS
- [ ] 최소 2개 실제 CLAP host PASS
- [ ] crash/data-loss blocker 0
- [ ] 2시간 soak와 audio underrun 0
- [ ] user-facing error/recovery UX
- [ ] beta EULA와 privacy 문서
- [ ] beta용 합법적 Voicebank

## G4 Release Candidate Gate

- [ ] VST3 validator PASS
- [ ] AU `auval` PASS
- [ ] 지원 DAW Matrix
- [ ] Windows Authenticode
- [ ] macOS codesign/notarization/staple
- [ ] clean OS install/update/uninstall
- [ ] Official Voicebank 01 acceptance
- [ ] Character 01 IP/상표/최종 자산 승인
- [ ] SBOM/third-party notices 최종 감사
- [ ] 사용자 매뉴얼·지원 정책

## G5 General Availability Gate

- [ ] reproducible release hash
- [ ] rollback 가능한 update channel
- [ ] 보안 취약점 대응 정책
- [ ] crash/support intake
- [ ] 공식 demo song과 sample project
- [ ] 라이선스·환불·상업 이용 FAQ
- [ ] release notes와 known issues

```text
SOURCE_READY ≠ BUILD_VERIFIED
BUILD_VERIFIED ≠ VALIDATOR_VERIFIED
VALIDATOR_VERIFIED ≠ HOST_VERIFIED
HOST_VERIFIED ≠ SIGNED/NOTARIZED
SIGNED/NOTARIZED ≠ INSTALL_VERIFIED
```
