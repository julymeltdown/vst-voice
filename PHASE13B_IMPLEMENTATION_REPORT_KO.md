# Project SEAM Phase 13B 구현 보고서

## 1. 목표

Phase 13B는 `Official Voicebank 01`과 `Character 01`을 실제 상용 콘텐츠로
승인할 때 필요한 자료·권리·검수 증적을 기계적으로 검증하는
**fail-closed 출시 엔지니어링 계층**을 구현한다.

이 단계는 계약이나 녹음 사실을 만들어내는 단계가 아니다. 저장소에는
현재 존재하는 사실만 기록한다.

```text
Phase 13B 엔지니어링 구현  PASS
Official Voicebank 01       BLOCKED
Character 01                BLOCKED
G5 제품 출시 Gate           BLOCKED
```

## 2. 구현된 기능

### 2.1 Evidence 검증

모든 `PASS` 증적은 다음 조건을 만족해야 한다.

- 저장소 또는 dossier root 기준 안전한 상대 POSIX 경로
- regular file이며 symbolic link가 아님
- root 밖으로 탈출하지 않음
- 비어 있지 않음
- 기본 64MiB 이하
- SHA-256 일치
- 증적 종류, 실행·승인 시각, reviewer 정보 존재
- PASS Gate에 FAIL 증적을 연결하지 않음

JSON 입력에도 파일 크기 상한, 빈 파일 거부, object root 강제를 적용했다.

### 2.2 Official Voicebank 01 Gate

다음 조건을 독립적으로 검사한다.

- `official=true`
- `contractedSinger=true`
- Voicebank ID·Version과 실제 Manifest 일치
- Bank root path·symlink·root escape 방어
- Unit 수, Pitch Layer, Unit Kind, Style, Phone inventory
- 실연자 계약과 권리 검토
- 디렉팅 녹음 세션과 장비 교정
- Retake 종료
- Marker·Pitch Mark·Loop QA
- Raw·Classic PSOLA·SpectralClassic·Stretch 청취 QA
- 서명된 `.seambank`와 설치 receipt
- 캐릭터 마케팅 권리 및 performer/character 분리 문구
- 사용자 상업 결과물 EULA
- 제품 책임자·법무 승인

현재 public-domain 인간 음성 Fixture는 `official=false`,
`contractedSinger=false`이므로 Official Voicebank로 승격되지 않는다.

### 2.3 Character 01 Gate

다음 조건을 독립적으로 검사한다.

- 최종 공개 이름이 placeholder가 아님
- Character ID·Version과 실제 Manifest 일치
- Character root path·symlink·root escape 방어
- 상표·도메인·소셜 핸들 clearance
- 디자이너·3D 아티스트 IP assignment
- 원본 provenance
- 정면·측면·후면 turnaround
- production low-poly model과 UV
- LOD 세트
- 표정·상태 portrait·animation
- 최종 key art
- merchandise·2차 창작 사용 정책
- performer/character 분리 문구
- 제품 책임자·법무 승인

현재 Character 01은 개발 concept와 blockout 단계이므로 상용 출시 Gate를
통과하지 않는다.

### 2.4 Character 개발 자산

현재 canonical 저폴리곤 이미지를 기반으로 다음 개발 전용 자산을
결정적으로 생성한다.

```text
key-art-1024.png
portrait-512.png
thumbnail-256.png
silhouette-256.png
palette.json
asset-manifest.json
```

각 파일의 SHA-256과 원본 SHA-256을 기록한다. Manifest에는 다음을
강제한다.

```json
{
  "developmentOnly": true,
  "productionStatus": "NOT_A_PRODUCTION_TURNAROUND"
}
```

따라서 이 자산은 최종 turnaround, production model, LOD, expression,
animation 또는 IP clearance 증적으로 사용될 수 없다.

### 2.5 Product G5 Gate

다음 세 계층을 모두 결합한다.

```text
Official Voicebank Gate
+ Character Gate
+ Phase 12C / Phase 13A / Phase 13B Mandatory Matrix
+ Final EULA / Voicebank License
= G5 General Availability Gate
```

외부 Target이 `PASS`라고 적혀 있더라도 실제 증적 파일과 SHA-256이
검증되지 않으면 Gate가 실패한다. Evidence root가 제공되지 않은 PASS도
fail-closed 처리한다.

### 2.6 결정적 패키지

두 종류의 ZIP을 생성한다.

1. `ProjectSEAM-0.13.1-content-development.zip`
   - public-domain 인간 음성 기술 Fixture
   - Character 01 개발 concept·blockout·파생 자산
   - `DEVELOPMENT_ONLY.txt`
   - canonical ordering, 1980-01-01 고정 timestamp

2. `ProjectSEAM-0.13.1-release-candidate-BLOCKED.zip`
   - Voicebank dossier
   - Character dossier
   - Product release dossier
   - G5 결과
   - `releaseEligible=false`

동일 입력으로 생성하면 byte-identical ZIP과 동일 SHA-256을 얻는다.
Checked-in 증적 JSON에는 workspace 절대 경로를 기록하지 않는다.

### 2.7 템플릿·법적/콘텐츠 작업 계약

다음 실제 외부 작업을 기록할 JSON 템플릿을 제공한다.

- Voice provider contract
- Character IP assignment
- Trademark/domain/social clearance
- Recording session log
- Retake closure
- Unit inventory profile
- Marker·Pitch·Loop QA
- 4 Renderer listening QA
- Final EULA approval
- Voicebank license approval
- Product/legal approval

템플릿 존재 자체는 `PASS` 증적이 아니다.

## 3. 캐릭터의 제품 내 위치

Character 01은 제품의 공식 보이스뱅크와 sample-splice 철학을 대표하는
시각 IP다. 가수나 밴드 보컬로 설정하지 않는다.

```text
사용 위치
- Welcome / 첫 실행
- Voicebank Browser와 Product Card
- 선택형 Character Dock
- Rendering / Complete / Warning / Error 상태
- Installer / Store / Documentation / Key Art

사용하지 않는 영역
- Phonemizer
- Unit Selection
- Renderer
- PCM Cache Identity
- Multichannel Routing
- Plug-in State Hash
- Exported WAV
```

캐릭터 패키지가 누락되거나 표시를 `Off`로 바꿔도 음성 결과는 동일해야
한다.

## 4. 현재 제품 Acceptance가 BLOCKED인 이유

### Official Voicebank 01

- 실제 실연자 선정과 서명 계약 없음
- 디렉팅 상용 녹음과 Retake 종료 없음
- 전체 목표 언어 Unit inventory 승인 없음
- Named reviewer의 4 Renderer listening QA 없음
- Official `.seambank` 서명·설치 승인 없음
- 최종 EULA·법무 승인 없음

### Character 01

- 최종 공개 이름 없음
- 상표·도메인·소셜 clearance 없음
- 디자이너·3D 아티스트 IP assignment 없음
- production turnaround·3D model·UV·LOD 없음
- 최종 표정·animation·key art 승인 없음
- 최종 merchandise/2차 창작 정책과 법무 승인 없음

### 선행 Mandatory Gate

Phase 12C·13A의 다음 실제 대상 검증도 여전히 필수다.

- 정확한 7,200초 full soak
- Windows/macOS 실제 runtime
- 공식 Validator
- VST3·AU 실제 대상 build와 validator
- Authenticode·Apple signing/notarization
- Clean OS installer 검증
- 선언된 상용 DAW matrix

## 5. 검증 명령

```bash
python3 -m unittest discover -s tests/phase13b -v
python3 scripts/verify_phase13b_contracts.py --root .
python3 scripts/generate_phase13b_evidence.py --root . \
  --output docs/phase13b/evidence
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
python3 scripts/verify_master_branch.py --root .
python3 tools/license-auditor/audit.py --root .
git diff --check
git fsck --full
```

## 6. 완료 경계

**Phase 13B 엔지니어링 범위는 완료 가능 상태**이며, 실제 제품 콘텐츠
Acceptance는 외부 증적이 들어올 때까지 의도적으로 차단된다.

다음 제품 단계는 UX·문서·Release Candidate 준비를 진행할 수 있지만,
Official Voicebank 01과 Character 01의 승인 없이 G5로 승격할 수 없다.
