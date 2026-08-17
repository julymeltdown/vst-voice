# Project SEAM 출시 준비 Gate

## Gate 정의

```text
G0 RESEARCH         기술 실험 가능
G1 FEATURE_ALPHA    핵심 수직 경로 실행 가능
G2 FEATURE_COMPLETE 계획된 제품 기능 구현 완료
G3 BETA             외부 사용자 테스트 가능
G4 RELEASE_CANDIDATE 상용 배포 후보
G5 GENERAL_AVAILABILITY 서명·설치·지원 가능한 정식 출시
```

현재 판정은 **G1 FEATURE_ALPHA**다.

## G2 Feature Complete Gate

- [ ] CLAP가 production PhraseRenderPipeline 사용
- [ ] 실제 `.seambank` 선택과 relink
- [ ] Phoneme/Unit/Pitch 직접 편집
- [ ] Host tempo/loop/seek 계약
- [ ] Multi-track/region
- [ ] 1–8채널 plugin routing
- [ ] Live voicebank note input 범위 확정
- [ ] 모든 기능의 state round trip

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
- [ ] 전체 지원 DAW Matrix
- [ ] Windows Authenticode
- [ ] macOS codesign/notarization/staple
- [ ] clean OS install/update/uninstall
- [ ] Official Voicebank 01 acceptance
- [ ] Character 01 IP/상표/최종 자산 승인
- [ ] SBOM/third-party notices 최종 감사
- [ ] 사용자 매뉴얼·지원 정책

## G5 General Availability Gate

- [ ] 출시 artifact reproducible hash
- [ ] rollback 가능한 update channel
- [ ] 보안 취약점 대응 정책
- [ ] crash/support intake
- [ ] 공식 데모 곡과 sample project
- [ ] 라이선스·환불·상업 이용 FAQ
- [ ] release notes와 known issues

## 출시 차단 원칙

다음 상태는 서로 대체할 수 없다.

```text
SOURCE_READY ≠ BUILD_VERIFIED
BUILD_VERIFIED ≠ VALIDATOR_VERIFIED
VALIDATOR_VERIFIED ≠ HOST_VERIFIED
HOST_VERIFIED ≠ SIGNED/NOTARIZED
SIGNED/NOTARIZED ≠ INSTALL_VERIFIED
```
