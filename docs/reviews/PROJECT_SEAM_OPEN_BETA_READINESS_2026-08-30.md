# Project SEAM 실제 Open Beta 준비도 심층 평가

- **평가일:** 2026-08-30
- **평가 대상:** `codex/external-beta-completion` / `970d159d06a2daa11932a9dbc22a337ecf9dbe25`
- **판정:** **HOLD / NO-GO — 지금 Open Beta를 열어서는 안 된다.**
- **현재 실질 준비도:** **약 42% (오차 범위 ±5%p).**
- **남은 발전량:** **약 55–65%**. 남은 일의 중심은 기능 추가보다 실제 Content, Distribution, target-machine QA, DAW certification, 운영과 외부 사용자 증적이다.

> 이 42%는 본 보고서의 weighted engineering judgment다. 저장소의 canonical promotion gate는 더 엄격하며, 현재 Usable Alpha는 0/20 PASS이고 External Beta candidate evidence도 없다. 따라서 formal promotion completion은 0%다.

## 1. 결론

Project SEAM은 더 이상 toy prototype이 아니다. 실제 Project model, Undo/Redo,
exact Voicebank identity, production sample-concatenative rendering, transport,
atomic save/export, autosave recovery, signed `.seambank`, CLAP editor, native
AppKit UI, CJK rendering, accessibility semantics, sanitizer coverage가 있다.

그러나 외부 사용자가 받게 될 하나의 제품으로 보면 아직 Feature Alpha다.
현재 가장 큰 문제는 다음과 같다.

1. **노래하게 해 줄 합법적이고 사용 가능한 Beta Voicebank가 없다.**
2. **현재 build를 하나의 immutable release candidate로 묶을 수 없다.**
3. **실제 배포 artifact가 notarized/stapled/clean-installed 상태가 아니다.**
4. **canonical standalone musician journey 20개가 전부 NOT_RUN이다.**
5. **Windows와 9개 DAW host tuple 증적이 사실상 0이다.**
6. **VoiceOver/Narrator, physical listening, 30/120-minute soak, external musician QA가 없다.**
7. **운영 제어와 support intake는 contract/validator 중심이며 실제 field loop가 없다.**

즉, 이 프로젝트의 병목은 이제 “합성 엔진이 있느냐”가 아니라 **신뢰할 수 있는
release candidate를 만들어 실제 사람과 실제 장비에서 반복 검증할 수 있느냐**다.

## 2. 평가 방법과 판정 기준

### 2.1 직접 확인한 현재 상태

- Git worktree는 clean이고 HEAD는 `970d159d`다.
- 현재 branch는 `codex/external-beta-completion`이며 remote와 upstream은 없다.
- current Release CTest 근거는 66/66 PASS, ASan/UBSan 63/63 PASS,
  TSan 63/63 PASS다.
- 이 평가에서 External Beta Python contract 116개와 관련 CTest 6개를 다시
  실행했고 모두 통과했다.
- `verify_usable_alpha_contract.py`는 `USABLE_ALPHA_CONTRACT=PASS`를 출력했지만,
  이는 JSON 구조와 hash policy가 일관되다는 뜻이다. 실제 gate는 `BLOCKED`이며
  20개 row 모두 `NOT_RUN`이다.
- Phase 13A verifier는 pipeline contract PASS와 함께
  `externalRuntimeResults=NOT_RUN`을 출력했다.
- Phase 13B verifier는 contract PASS와 함께 Voicebank/Character baseline이
  blocked임을 확인했다.

### 2.2 Weighted readiness model

| 영역 | 가중치 | 현재 점수 | 가중 기여 | 근거 |
| --- | ---: | ---: | ---: | --- |
| Core synthesis/authoring implementation | 20% | 82% | 16.4 | production renderer, editor commands, transport, export, lifecycle tests |
| Packaged standalone musician journey | 15% | 45% | 6.8 | code와 controller tests는 강하지만 canonical physical journey 0/20 |
| Beta Voicebank, content, rights | 15% | 10% | 1.5 | gate/tooling은 있으나 실제 dossier/package/rights가 비어 있음 |
| Signing, installer, update, rollback | 15% | 25% | 3.8 | source pipeline과 일부 local signing은 있으나 notarized installed candidate 없음 |
| Target OS and DAW compatibility | 15% | 20% | 3.0 | local/internal host evidence는 있으나 Windows와 9-host matrix 미완료 |
| Security and data integrity | 10% | 65% | 6.5 | fail-closed trust, path safety, atomic I/O가 강하지만 shipped candidate audit 없음 |
| Accessibility and human UX validation | 5% | 45% | 2.3 | semantic/AppKit automation은 강하나 assistive-tool/human evidence 없음 |
| Operations, support, cohort control | 5% | 30% | 1.5 | schemas/validators는 강하나 live monitoring/intake/field records 없음 |
| **총합** | **100%** |  | **41.8%** | **반올림 42%** |

이 모델은 code completeness와 release proof를 함께 본다. 반대로 canonical gate만
보면 `UA 0/20`, `EB candidate evidence 없음`이므로 promotion readiness는 0%다.

## 3. 지금 실제로 잘 된 부분

### 3.1 Production core

- Standalone과 CLAP가 shared `AuthoringRuntime`과 production renderer를 사용한다.
- Note/Lyric/Phoneme/Unit/Seam/Pitch 편집이 canonical Project state와 Undo/Redo에
  연결돼 있다.
- Render worker, stale revision rejection, cache, bounded callback publication,
  multichannel routing이 분리돼 있다.
- Export는 `ProductionProjectRenderer`를 사용하고 staging/journal/atomic publication
  경로를 가진다.

이는 README의 “final export가 미구현”이라는 오래된 표현과 다르다. 현재 문제는
export code 부재가 아니라 packaged app에서 실제 곡 master/stem을 export하고
external player로 검증한 acceptance evidence 부재다.

### 3.2 Project/data safety

- Project schema migration, durable atomic write, autosave generation pruning,
  corruption rejection, symlink rejection, failed-open non-mutation tests가 있다.
- Export replacement/recovery transaction coverage도 넓다.
- Current-version recovery architecture는 beta 후보의 강점이다.

### 3.3 Voicebank trust and supply-chain design

- `.seambank`는 data-only format이며 traversal, links, hidden/executable content,
  duplicate/overlap/limit 위반을 거부한다.
- 설치는 명시적으로 trusted public key를 요구하고 exact
  `(id, version, contentHash)`를 다시 확인한다
  (`voicebank_installer_service.cpp:79-173`).
- 같은 ID/version에 다른 synthesis content가 오면 conflict로 거부한다.
- Untrusted/development candidates는 production trust와 분리된다.

이 부분은 116개의 External Beta contract test에서도 fail-closed behavior가
상당히 잘 검증된다. 문제는 validator가 아니라, validator에 넣을 실제 Beta bank가
없다는 것이다.

### 3.4 Native UI foundation

- Native editor design rubric은 overlap, CJK detail, responsive toolbar,
  exact character gating, reduced motion을 상당히 잘 닫았다.
- 10,000-note paint benchmark는 16.7 ms budget보다 훨씬 낮은 약 2.19 ms p95였다.
- Stable semantic tree, focus, CJK IME, AppKit bridge, 512-note virtualization이 있다.

이 결과는 UI engineering maturity를 높이지만, independent musician usability,
VoiceOver/Narrator certification 또는 Beta readiness를 대신하지는 않는다.

## 4. P0 — Beta를 즉시 막는 문제

### P0-1. Rights-cleared Beta Voicebank 부재

`docs/voicebank/beta-voicebank-01-dossier.json`은 `BLOCKED`이고 package hash,
signature delegation, inventory coverage, rights permission, reference-song receipt가
비어 있다. Canonical gate도 실제 `.seambank`와 private rights record가 외부 release
input이라고 명시한다
([BETA_VOICEBANK_ACCEPTANCE.md](../voicebank/BETA_VOICEBANK_ACCEPTANCE.md)).

이것은 가장 중요한 non-code critical path다. Engine이 아무리 좋아도 합법적으로
재배포할 충분한 발음 coverage의 bank가 없으면 외부 musician은 제품을 정상 평가할
수 없다.

필수 완료물:

- Voice provider agreement와 privacy-safe evidence
- recording/retake/session logs
- 실제 Japanese coverage inventory와 supported pitch/style 범위
- marker/pitch/loop QA와 네 renderer listening QA
- signed `.seambank`, clean-install receipt, canonical reference song
- recording transformation, redistribution, end-user rendered-audio 권한

### P0-2. Candidate identity가 현재 source와 일치하지 않음

현재 HEAD는 `970d159d...`지만 `build-release-current/CMakeCache.txt`와 built
`Info.plist`의 `ProjectSEAMSourceCommit`은
`776d43e2d919ffbe2db4ffbbce137521998ea703`이다. Build ID는
`0.13.1-local`, epoch는 `0`이다.

`CMakeLists.txt:5-21`은 release automation이 source/build identity를 명시적으로
주입한다는 구조지만, incremental local build는 이전 cache identity를 유지한다.
따라서 “이 screenshot/test/app가 exact final SHA에 묶였다”는 주장을 binary 자체가
증명하지 못한다.

필수 수정:

- candidate configure 시 HEAD, non-local build ID, trusted epoch를 강제 주입
- stale cache identity면 configure/build를 즉시 실패
- app, CLAP, VST3, AUv2, installer, SBOM, evidence record에 같은 identity 사용
- build 후 binary/plist/descriptor/runtime probe가 candidate root와 일치하는지 검증

### P0-3. 배포 가능한 macOS/Windows artifact 부재

현재 `build-release-current/Project SEAM.app`은 arm64 Mach-O지만 linker ad-hoc
signature다. 직접 확인 결과 strict `codesign` 검증이 실패했고 notarization ticket이
없었다. `.omo/runtime-release`의 local Developer ID staging app은 signature 자체는
유효하지만 Gatekeeper가 `Unnotarized Developer ID`로 거부했고, source commit도
위의 stale commit이다.

저장소에는 하나의 authoritative `.pkg`/`.dmg`/Windows `.exe` candidate가 없다.
Apple은 direct-distribution app, plug-in, installer를 각각 Developer ID,
Hardened Runtime, secure timestamp, notarization 대상으로 본다
([Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution),
[Apple Gatekeeper](https://support.apple.com/guide/security/sec5599b66df/web)).

Windows 쪽은 workflow/source만 있고 signed x64 installer와 runtime evidence가 없다.

### P0-4. 실제 standalone musician journey 0/20

Canonical contract는 Apple Silicon Finder launch부터 project creation, note/lyric,
technical edits, production audio, transport, save/reopen, audio parity, crash recovery,
voicebank relink, master/stem export, external player verification, 30-minute session까지
20개를 요구한다
([USABLE_ALPHA_ACCEPTANCE.md:23-60](../product/USABLE_ALPHA_ACCEPTANCE.md)).

Machine mirror는 20개 모두 `NOT_RUN`, evidence empty다. Controller/unit tests가
많아도 native dialogs, permissions, EULA first launch, device negotiation, forced process
termination, actual file association, external player 같은 OS surface는 독립적으로 실패할
수 있다.

### P0-5. Target OS와 DAW matrix 미완료

External Beta contract는 최소 9개 tuple을 요구한다.

- macOS: REAPER CLAP/VST3, Bitwig CLAP/VST3, Logic Pro AUv2
- Windows: REAPER CLAP/VST3, Bitwig CLAP/VST3

현재 checked-in completed host record는 없다. Internal CLAP host나 validator PASS는
real DAW scan, GUI lifecycle, state recall, offline bounce, host isolation, installed-byte
behavior를 증명하지 않는다. CLAP 공식 validator도 그 범위를 대신한다고 주장하지
않는다
([clap-validator](https://github.com/free-audio/clap-validator)).

### P0-6. Physical/audio/accessibility/human acceptance 부재

- VoiceOver, Accessibility Inspector, Narrator/UIA Verify, Inspect는 모두 `NOT_RUN`
  (`docs/product/accessibility-test-matrix.json:4-16`).
- 실제 musician listening과 external-player verification이 없다.
- signed-installed product의 30-minute/120-minute physical soak가 없다.
- Device loss/reconnect, sleep/wake, buffer/sample-rate change, actual MIDI controller
  evidence가 없다.

Deterministic audio mode는 source에서 명시적으로 nonphysical이다. Native visual QA와
short fixture energy는 이 gate를 대체할 수 없다.

### P0-7. External Beta authorization과 governed archive가 없음

`EXTERNAL_BETA_RELEASE_AUTHORIZATION.md:16-18`은 GO authorization과 signed
predecessor가 없다고 명시한다. `external-beta-acceptance.json`은 aggregate
`BLOCKED`, evidence `[]`다.

READY에는 candidate root, named stage transformations, signed/installed descendant,
raw archive bytes, machine/workload identity, independent roles와 external immutable
anchor가 필요하다
([EXTERNAL_BETA_ACCEPTANCE.md:14-27](../product/EXTERNAL_BETA_ACCEPTANCE.md)).

## 5. P1 — 제한된 Beta 전까지 해결해야 할 Code/Product 문제

### P1-1. Support export가 두 번째부터 실패할 수 있음

`NativeEditorApp`은 항상 `Support/latest-diagnostic.zip`으로 export한다
(`native_editor_app.cpp:703-729`). `SupportBundleService`는 destination이 이미 있으면
Conflict로 거부한다 (`support_bundle.cpp:345-358`). 첫 export 뒤 파일을 지우지 않으면
다음 support action이 실패할 수 있다.

해결: timestamp/candidate-bound filename 또는 preview + explicit atomic replace/save-as.

### P1-2. User attachment privacy class가 과도하게 넓음

Generated diagnostics는 allowlist와 privacy filter가 잘 되어 있다
(`support_bundle.cpp:78-140`). 하지만 consented attachment는 regular-file/size/name만
검사한 뒤 bytes를 복사하고, 전체 manifest를 `ExportSafe`로 표시한다
(`support_bundle.cpp:267-305`). Project, lyric, audio, raw log, secret이 attachment로
들어갈 수 있으므로 generated diagnostics와 attachment privacy class를 분리해야 한다.

### P1-3. Release operation approval가 cryptographic authority에 묶이지 않음

`tools/external_beta/operations.py:49-105`는 actor role string, `auditPassed` boolean,
approval list로 state를 바꾼다. Candidate/root consistency는 보지만 audit와 approval
record의 signature/hash-chain을 직접 요구하지 않는다.

해결: signed audit and approval digest를 candidate root와 이전 decision digest에
연결하고 append-only authority에서 검증한다.

### P1-4. Pause/revoke가 배포 client와 연결된 증적이 없음

Local operations snapshot은 `DISTRIBUTION_PAUSED`/`REVOKED`를 표현하지만 installed
client나 updater가 그 authority를 소비한다는 end-to-end 증거가 없다. 이미 배포한
취약 candidate를 실제로 멈출 수 있어야 한다.

### P1-5. Soak runner의 profile typo가 5초 smoke로 내려감

`phase12c/src/soak_runner.cpp:104-117`은 정확히 `full`일 때만 7,200초를 사용하고
그 외 임의의 profile은 5초가 된다. Unknown option도 사실상 무시된다. `ful` 같은
오타가 green-looking smoke artifact를 만들 수 있다.

해결: enum parser, unknown rejection, explicit full-mode token, heartbeat와 watchdog.

### P1-6. Collector보다 validator가 앞서 있음

`run_external_beta_product_soak.py`는 이미 만들어진 record를 검증할 뿐 실제 installed
product metric series를 수집하지 않는다. Release, install, host, cohort 도구도 전반적으로
“좋은 evidence가 왔다고 가정하고 검증”하는 쪽이 “현장에서 evidence를 생산하고
보존”하는 쪽보다 앞서 있다.

### P1-7. 운영 feedback loop가 없음

Privacy-first/no-telemetry 방향은 합리적이다. 그러나 그 대가로 named cohort check-in,
support handoff, issue triage, alert acknowledgement, escalation owner, pause/revoke SLA가
더 중요해진다. 현재 app의 OpenSupport는 local ZIP을 만들 뿐 실제 intake/ticket
handoff가 검증되지 않았다.

## 6. P2 — Beta 중 비용을 키울 구조적 위험

### 6.1 Native UI complexity concentration

다음 파일은 변경 충돌과 회귀 위험이 크다.

- `editor_controller.cpp`: 약 3,066 lines
- `test_native_ui.cpp`: 약 2,664 lines
- `editor_scene.cpp`: 약 1,566 lines
- `native_window_appkit.mm`: 약 1,231 lines
- `application_controller.cpp`: 약 1,021 lines

지금 당장 전체 refactor가 P0는 아니다. 하지만 Beta 전에 controller를 input mode,
selection/edit commands, accessibility dispatch, overlay/panel coordination 단위로
분리하고 critical state-machine tests를 보강하지 않으면 field bug fix 속도가 느려진다.

### 6.2 Cross-version migration contract 부족

Project schema 1–7 migration과 future schema rejection은 있다. 그러나 actual predecessor
package에서 current package로 update한 뒤 project, autosave, catalog, media, CLAP state,
host rescan을 한꺼번에 검증한 fixture가 없다. Beta가 시작되면 N→N+1이 곧 제품 기능이 된다.

### 6.3 Documentation/status coherence

- README는 export implementation과 current blockers를 오래된 표현으로 설명한다.
- English `RELEASE_READINESS.md`는 이미 구현된 live articulation이 아직 implementation
  blocker인 것처럼 쓴다.
- “USABLE_ALPHA_CONTRACT=PASS”, “PHASE13B_CONTRACT=PASS”는 readiness PASS로
  오해하기 쉽다.
- README/STATUS는 `master only`를 말하지만 현재 checkout은 다른 branch이고 remote가 없다.
- 기존 “65–70% practical readiness” 수치는 canonical 0/20 gate와 denominator가 다르고
  업데이트 시점도 오래됐다.

외부 tester와 release operator가 보는 하나의 status page를 만들고
`CONTRACT_VALID`, `IMPLEMENTED`, `TARGET_PASS`, `BETA_READY`를 분리해야 한다.

## 7. 왜 Test가 Green인데 Beta가 42%인가

현재 test suite는 “잘못된 evidence나 hostile input을 거부하는 법”을 매우 잘 검증한다.
External Beta contract test 116개가 그 예다. 그러나 이 테스트들은 다음을 하지 않는다.

- 실제 singer와 계약하고 recording하기
- signed candidate를 clean machine에 설치하기
- Gatekeeper/Windows trust chain을 통과하기
- REAPER/Bitwig/Logic에서 musical session을 진행하기
- 실제 audio device와 MIDI controller를 견디기
- musician이 결과를 듣고 usable하다고 판단하기
- support incident를 접수하고 update/revoke를 배포하기

정리하면 **verification framework maturity가 product evidence maturity보다 훨씬 앞서 있다.**
이것은 나쁜 architecture가 아니라 다음 milestone의 성격을 잘못 이해하면 생기는 함정이다.
다음 단계는 schema를 더 만드는 것이 아니라 schema가 요구하는 실제 evidence를 생산하는 것이다.

## 8. 권장 Scope 전략

### 선택 A — 지금 바로 Public/Open Beta

**거부.** Content, installer, Windows, host, accessibility, support, update/revoke,
external evidence가 동시에 비어 있어 field risk가 너무 크다.

### 선택 B — 별도 Mac-only Private Alpha

가장 빠른 학습 경로다. 단, 이를 현재 `EXTERNAL_BETA_READY`라고 부르면 안 된다.
별도 contract로 다음처럼 제한한다.

- Apple Silicon macOS 13+ 한정
- Standalone + CLAP/REAPER 한정, 또는 standalone-only
- named invite 5–10명
- rights-cleared non-official Beta bank 한 개
- notarized/stapled PKG 한 개
- manual update만 허용하고 no in-app updater를 명확히 선언
- daily check-in, known issues, immediate revoke channel

이 scope라도 signing/notarization, privacy, content rights, project recovery, support는 생략할
수 없다. 줄어드는 것은 host/platform breadth다.

### 선택 C — 저장소가 정의한 Closed External Beta

정확한 목표지만 가장 비싸다. macOS와 Windows, 9개 host tuple, signed installs,
standalone/soak, governed archive, cohort closure를 모두 요구한다. Public beta 전에 이 gate를
먼저 통과시키는 것이 안전하다.

## 9. 권장 실행 Roadmap

### Phase 0 — Release truth 정리 (1–2주)

1. 지원 scope를 확정한다: Mac private alpha인지, two-platform External Beta인지.
2. build identity cache drift를 차단하고 one-candidate root를 만든다.
3. remote/upstream, protected release branch, CI source authority를 복구한다.
4. stale README/status/PASS labels를 정리한다.
5. current P0/P1 defect ledger와 owner를 만든다.

**Exit:** binary identity = candidate HEAD, one immutable candidate, no ambiguous `signed-v2.zip`.

### Phase 1 — Beta Voicebank와 legal (6–12주, 병렬 critical path)

1. provider/rights 계약
2. recording, retake, inventory, marker/pitch/loop QA
3. listening QA와 reference song
4. signed `.seambank`, trust delegation, install receipt
5. public license/EULA language 승인

**Exit:** Beta bank dossier PASS. Code만으로 단축할 수 없는 critical path다.

### Phase 2 — Distributable artifacts (2–4주)

1. macOS nested signing → PKG → notarization → stapling
2. Gatekeeper clean install/reinstall/update/interruption/uninstall
3. Windows Authenticode/timestamp/installer if in scope
4. SBOM, notices, package hashes, rollback predecessor
5. app/plugin/installer identity parity

**Exit:** exact installed bytes and source identity가 one candidate graph에 묶임.

### Phase 3 — Usable Alpha 20-row closure (3–6주)

1. 실제 곡과 physical audio로 20개 row 수행
2. forced-crash recovery, relink, external WAV verification
3. 30-minute session과 latency/memory/xrun thresholds
4. 발견된 defects 수정 후 candidate rebuild/retest

**Exit:** 20/20 hash-bound PASS.

### Phase 4 — Platform/host/accessibility certification (4–8주)

1. macOS/Windows installed standalone matrix
2. 9개 DAW tuple
3. official validators on delivered signed bytes
4. VoiceOver/Accessibility Inspector/Narrator/UIA
5. named audio and MIDI devices, device loss, sleep/wake
6. N→N+1 predecessor migration

**Exit:** target and host records PASS, Blocker/Critical 0.

### Phase 5 — Operations and closed cohort (3–6주)

1. support intake, sanitized attachment policy, repeated export fix
2. candidate-bound feedback/incident records
3. monitoring/check-in/SLO and escalation owner
4. updater/operation authority integration, pause/revoke rehearsal
5. 5–20명 closed cohort, 2주 이상 real sessions

**Exit:** EB-001–EB-008 READY evidence, no unresolved critical incident.

### Phase 6 — Public/Open Beta 전환 (추가 4–8주)

1. closed cohort defect burn-down
2. broader host/device sampling
3. public privacy/support/known-issues publication
4. update and rollback drill
5. capacity/support load review

**Exit:** public-link distribution을 중단·복구·지원할 수 있는 운영 능력 확보.

## 10. 예상 기간

다음은 명시적 추정이며 contract evidence가 아니다.

| 목표 | 가정 | 현실적 기간 |
| --- | --- | ---: |
| Mac-only named Private Alpha | Beta bank/certs 작업 병렬, 1–2 engineers + QA | **10–16주** |
| 현재 contract의 Closed External Beta | 2–3 engineers + release/QA + content/legal | **16–28주 (4–7개월)** |
| Public/Open Beta | closed cohort 성공 후 확대 | **총 24–36주 (6–9개월)** |
| 1인 개발로 동일 scope | 외부 content/legal은 별도 | **약 8–12개월 이상** |

Beta bank contract/recording이 지연되면 전체 일정도 그대로 지연된다. 반대로 Windows와
9-host breadth를 별도 future gate로 분리하고 macOS private alpha로 명명하면 첫 외부 학습은
앞당길 수 있다.

## 11. GO 조건

Public/Open Beta GO는 최소 다음이 모두 참일 때만 가능하다.

- [ ] rights-cleared Beta bank dossier PASS
- [ ] one candidate identity가 source/binaries/installers/evidence에 일치
- [ ] macOS notarized/stapled clean install PASS
- [ ] Windows가 scope면 signed x64 clean install PASS
- [ ] Usable Alpha 20/20 PASS
- [ ] required host tuples PASS
- [ ] physical audio/MIDI and soak thresholds PASS
- [ ] VoiceOver/Narrator target runs PASS
- [ ] support/privacy/EULA approved and actually reachable
- [ ] update, rollback, pause, revoke rehearsal PASS
- [ ] immutable archive audit PASS
- [ ] Blocker/Critical 0, release-role authorization GO
- [ ] closed cohort에서 실제 external sessions 완료

하나라도 빠지면 Open Beta가 아니라 internal/dev preview 또는 explicitly scoped private
alpha로 표기해야 한다.

## 12. 최종 POV

**Grade: HOLD — public beta를 지금 열지 말고, 먼저 macOS private alpha 또는 현재
closed-beta contract를 닫아야 한다.**

Project SEAM의 core architecture는 계속 투자할 가치가 있다. 엔진과 editor를 다시 쓰는
것이 답은 아니다. 다음 milestone은 새로운 abstraction이나 schema를 추가하는 것이 아니라,
하나의 rights-cleared content + 하나의 immutable candidate + 실제 installed-user evidence를
끝까지 연결하는 것이다.

### Reversal trigger

이 판정은 다음 증거가 생기면 `HOLD`에서 `TRIAL/GO`로 뒤집힌다.

1. Beta bank와 candidate authorization PASS
2. signed/notarized clean-installed candidate
3. Usable Alpha 20/20
4. target OS/host/accessibility/soak 증적
5. 실제 closed cohort에서 Blocker/Critical 0

## 13. External 기준 자료

- [Apple: Distributing software on macOS](https://developer.apple.com/macos/distribution/)
- [Apple: Notarizing macOS software](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- [Apple: Gatekeeper and runtime protection](https://support.apple.com/guide/security/sec5599b66df/web)
- [CLAP specification and lifecycle](https://github.com/free-audio/clap)
- [Official clap-validator](https://github.com/free-audio/clap-validator)
- [Apple TestFlight overview](https://developer.apple.com/help/app-store-connect/test-a-beta-version/testflight-overview)
- [Apple TestFlight privacy](https://www.apple.com/legal/privacy/data/en/test-flight/)
