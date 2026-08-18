# Project SEAM 출시 준비 Gate

현재 판정은 **G1 FEATURE_ALPHA**다.

## G2 Feature Complete

- [x] production Phrase pipeline와 trusted Voicebank resolution
- [x] direct Phoneme/Unit/Pitch editing
- [x] multi-track/region/1~8-channel routing
- [x] Voicebank-driven live articulation 구현
- [ ] 공식 CLAP validator 실제 PASS
- [ ] Linux 336-case matrix 최종 증적 PASS
- [ ] 정확한 7,200초 full soak PASS

## G3 Beta

- [ ] Windows runtime PASS
- [ ] macOS runtime PASS
- [ ] REAPER 실제 PASS
- [ ] Bitwig Studio 실제 PASS
- [ ] Logic Pro 실제 PASS
- [ ] crash/data-loss blocker 0
- [ ] Beta EULA·privacy·합법적 Beta Voicebank

## G4 Release Candidate

- [ ] Linux·Windows·macOS VST3 validator PASS
- [ ] macOS AUv2 build와 `auval` PASS
- [ ] 선언된 전체 상용 DAW Matrix PASS
- [ ] Windows Authenticode와 timestamp PASS
- [ ] Apple codesign·notarization·stapling PASS
- [ ] Windows/macOS clean install·update·uninstall PASS
- [ ] Official Voicebank 01 acceptance
- [ ] Character 01 IP/상표/최종 asset 승인
- [ ] SBOM·third-party notice 최종 감사

## G5 General Availability

- [ ] mandatory validation unresolved count = 0
- [ ] reproducible release hash와 rollback channel
- [ ] support/crash/security response process
- [ ] 공식 demo song·sample project
- [ ] 최종 EULA·Voicebank License·commercial-use FAQ

```text
SOURCE_READY ≠ TARGET_BUILD_PASS
TARGET_BUILD_PASS ≠ VALIDATOR PASS
VALIDATOR PASS ≠ HOST PASS
HOST PASS ≠ SIGNED/NOTARIZED
SIGNED/NOTARIZED ≠ CLEAN INSTALL PASS
```
