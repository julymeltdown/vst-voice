# Project SEAM Phase 13B 구현 보고서

## 1. 단계 목적

Phase 13B는 Official Voicebank 01과 Character 01을 실제 상용 제품으로
승격할 때 필요한 콘텐츠·권리·품질 증적을 **기계적으로 검증하고,
증적이 없으면 반드시 차단하는 release-engineering 계층**을 구현한다.

이번 단계는 실제 실연자 계약, 녹음 세션, 상표 조사 또는 법무 승인을
만들어 내는 단계가 아니다. 코드로 검증 가능한 형식·해시·자산·패키지
계약은 구현했으며, 외부 사실이 필요한 항목은 `NOT_RUN` 또는
`BLOCKED`로 유지한다.

```text
엔지니어링 구현 상태  PASS
Official Voicebank 01 BLOCKED
Character 01          BLOCKED
G5 제품 Acceptance    BLOCKED
```

## 2. 구현 기능

### 2.1 증적 신뢰 경계

- 상대 POSIX 경로만 허용
- 절대 경로, `.`·`..`, 역슬래시 차단
- symlink 차단
- dossier root 밖으로 나가는 경로 차단
- regular file 확인
- 파일당 64MiB 상한
- SHA-256 실제 byte 검증
- 검수 시각과 검수자 필수
- PASS Gate의 빈 증적 차단
- Phase 13A Host 인증 형식의 로그별 SHA-256 재검증

### 2.2 Official Voicebank 01 Dossier

다음 항목을 모두 독립 Gate로 검증한다.

```text
performer-contract
rights-review
recording-session-logs
microphone-chain-calibration
complete-unit-inventory
retake-closure
marker-pitch-loop-qa
renderer-listening-qa
signed-seambank
installation-receipt
character-marketing-rights
performer-character-separation
commercial-user-output-eula
product-owner-approval
legal-approval
```

Voicebank inventory audit는 Unit 수, Pitch Layer, Unit Kind, Style, Phone,
WAV 경로 안전성을 확인한다. `renderer-listening-qa`는 Raw, Classic PSOLA,
SpectralClassic, Stretch 네 Renderer가 모두 PASS여야 한다.

공개 도메인 인간 음성 fixture는 `official=false`,
`contractedSinger=false`를 유지하며 Official Voicebank 01로 승격할 수
없다.

### 2.3 Character 01 Dossier

다음 항목을 모두 독립 Gate로 검증한다.

```text
public-name
trademark-clearance
domain-clearance
social-handle-clearance
ip-assignment
source-provenance
front-side-back-turnaround
production-low-poly-model
lod-set
expression-set
animation-set
runtime-state-assets
key-art
merchandise-policy
voice-character-separation
product-owner-approval
legal-approval
```

`Character 01`, `TBD`, `TODO`와 같은 placeholder 이름은 최종 이름으로
허용하지 않는다. Character root의 symlink 또는 dossier root 이탈도
거부한다.

### 2.4 Deterministic Character 개발 자산

현재 선택된 canonical 저폴리곤 이미지를 기준으로 다음 개발 자산을
결정적으로 생성한다.

```text
key-art-1024.png
portrait-512.png
thumbnail-256.png
silhouette-256.png
palette.json
asset-manifest.json
```

모든 산출물은 source SHA-256과 output SHA-256을 기록한다. 자산은
`developmentOnly=true`,
`productionStatus=NOT_A_PRODUCTION_TURNAROUND`로 고정된다. 이는 최종
정면·측면·후면 turnaround, production topology, rig, animation 승인을
대체하지 않는다.

### 2.5 Deterministic Development Content Bundle

다음 개발 콘텐츠를 deterministic ZIP으로 묶는다.

- 공개 도메인 인간 음성 기술 fixture
- Character 01 개발 자산
- provenance와 license
- 파일별 SHA-256 manifest
- `DEVELOPMENT_ONLY.txt`

고정 timestamp, 정렬된 entry, 고정 permission을 사용한다. symlink,
숨김 경로, path traversal, 파일·전체 크기 상한 초과를 거부한다.

### 2.6 G5 Fail-closed Release Gate

G5는 다음 조건이 모두 충족될 때만 PASS한다.

1. Official Voicebank 01 Dossier ACCEPTED
2. Character 01 Dossier ACCEPTED
3. Phase 12C mandatory target 전부 PASS + 실제 증적
4. Phase 13A mandatory target 전부 PASS + 실제 증적
5. Phase 13B mandatory target 전부 PASS + 실제 증적
6. 최종 EULA PASS + 실제 증적
7. Voicebank License PASS + 실제 증적
8. unresolved mandatory count 0

단순히 JSON에 `runtimeResult=PASS`를 적는 것으로는 통과할 수 없다.
간단한 evidence record와 Phase 13A Host certification record 모두 실제
파일의 SHA-256을 다시 계산한다.

## 3. 산출물

```text
docs/phase13b/official-voicebank-01-dossier.json
docs/phase13b/character-01-dossier.json
docs/phase13b/product-release-dossier.json
docs/phase13b/mandatory-validation-matrix.json
assets/character-01/production-development/
docs/phase13b/evidence/ProjectSEAM-0.13.1-content-development.zip
docs/phase13b/evidence/ProjectSEAM-0.13.1-release-candidate-BLOCKED.zip
```

계약, 녹음, retake, listening QA, IP 양도, clearance, EULA 승인용 문서와
JSON 템플릿도 `docs/legal/templates`, `docs/voicebank/templates`,
`docs/brand`에 추가했다.

## 4. 필수 후속 검증

다음 문서는 선택 사항이 아닌 출시 차단 조건이다.

```text
docs/phase13b/MANDATORY_FUTURE_VALIDATION_KO.md
docs/phase13b/MANDATORY_OFFICIAL_VOICEBANK_AND_CHARACTER_VALIDATION_KO.md
docs/phase13b/mandatory-validation-matrix.json
```

Phase 12C의 공식 validator·7,200초 soak, Phase 13A의 실제 OS·DAW·서명·
notarization·installer 검증, 실제 Voicebank 계약·녹음, Character 상표·
IP·생산 자산 승인이 모두 실제 증적과 함께 완료되어야 한다.

## 5. 현재 완료 경계

Phase 13B의 **엔지니어링 도구와 개발 자산 생성 범위는 구현됐다.**
그러나 외부 계약·녹음·권리·상표·법무 증적이 없으므로 제품 Acceptance는
의도적으로 BLOCKED다. 공개 도메인 음성이나 현재 캐릭터 개발 이미지를
Official release 증적으로 오인하지 않는다.
