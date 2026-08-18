# Phase 13A 실제 Host 인증 기록 방법

이 문서는 체크리스트가 아니라 **실제 실행 결과를 증거와 함께 기록하는 절차**다.
소스 구현, CI 구성, 바이너리 생성만으로는 Host 인증 `PASS`를 만들 수 없다.

## PASS에 필요한 실제 실행 항목

실제 대상 운영체제와 실제 DAW에서 다음 항목을 전부 수행해야 한다.

```text
scan
installDiscovery
guiLifecycle
stateRestore
transport
tempoAutomation
offlineExport
unloadReload
channelMatrix
sampleRateMatrix
bufferMatrix
projectReopen
```

각 값은 모두 정확히 `PASS`여야 한다. 하나라도 실행하지 않았거나 실패했다면
`runtimeResult`는 `NOT_RUN`, `BLOCKED` 또는 `FAIL`이어야 한다.

## 필수 메타데이터

```text
targetId
OS version
DAW name and exact version
plug-in format
plug-in SHA-256
execution timestamp
executor
raw logs/screenshots/project files
```

`docs/phase13a/host-certification-record-template.json`을 복사한 뒤 실제 값으로
채운다. 증거 경로는 record 파일의 evidence root 아래 상대 경로로 기록한다.

```bash
python3 tools/phase13a/host_certification.py \
  --matrix docs/phase13a/mandatory-validation-matrix.json \
  --record evidence/reaper/result-record.json \
  --evidence-root evidence/reaper \
  --output evidence/reaper/updated-validation-matrix.json
```

도구는 증거 파일의 SHA-256을 다시 계산한다. 경로 탈출, 빈 증거 파일,
누락된 검사, 잘못된 플러그인 SHA 또는 허위 PASS record는 거부한다.

## Release Gate 연결

```bash
python3 tools/phase13a/release_gate.py check \
  --matrix evidence/reaper/updated-validation-matrix.json --gate G3
```

한 Host의 PASS만으로 전체 Gate가 열리지는 않는다. 해당 Gate에 필수인 모든
실제 Target row가 PASS여야 한다.
