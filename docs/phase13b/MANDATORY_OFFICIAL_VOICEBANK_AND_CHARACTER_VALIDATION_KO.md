# 반드시 수행해야 하는 Official Voicebank 01·Character 01 검증

이 문서의 검증은 선택 사항이 아니다. 실제 계약서, 실제 녹음 세션, 실제 검수자, 실제 상표·권리 확인, 실제 서명 패키지와 실제 설치 증적이 저장되기 전에는 Official Voicebank 01 또는 Character 01을 `PASS`, `ACCEPTED`, Beta, RC, GA로 표시할 수 없다.

## 상태 규칙

```text
구현 상태: NOT_STARTED | SOURCE_READY | CI_CONFIGURED | TARGET_BUILD_PASS
검증 결과: NOT_RUN | BLOCKED | FAIL | PASS
```

`SOURCE_READY`와 `TARGET_BUILD_PASS`는 법적·콘텐츠 검증 `PASS`가 아니다.

## Voicebank 필수 항목

- 실연자 신원 확인과 서명 계약
- 녹음물·실연·편집·음소 절단·피치/길이/포먼트 변형 권리
- 디렉팅 녹음 세션과 장비 교정 기록
- 전체 목표 음소·피치 레이어·발성 목록
- 재녹음 종료와 수락 기록
- Marker·Pitch Mark·Loop QA
- Raw·PSOLA·Spectral·Stretch 청취 QA
- 서명된 `.seambank`와 설치 receipt
- 캐릭터 마케팅 권리와 실연자/캐릭터 분리 문구
- 사용자 상업 결과물 EULA
- 제품 책임자·법무 승인

## Character 필수 항목

- 최종 공개 이름
- 실제 상표·도메인·소셜 계정 clearance
- 디자이너·3D 아티스트의 IP 양도 또는 상업 이용 계약
- 원본 provenance
- 정면·측면·후면 turnaround
- production low-poly model, UV, LOD
- 표정·상태 portrait·animation
- 최종 key art와 굿즈 사용 정책
- Voice provider와 Character가 동일 인물이라는 오해를 막는 문구
- 제품 책임자·법무 승인

## 출시 차단

`mandatory-validation-matrix.json`의 mandatory row 중 하나라도 `NOT_RUN`, `BLOCKED`, `FAIL`, 증적 누락 또는 hash mismatch이면 G5는 실패해야 한다.
