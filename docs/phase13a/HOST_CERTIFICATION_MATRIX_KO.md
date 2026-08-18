# Project SEAM 상용 Host 인증 Matrix

다음 Host 테스트는 실제 제품 승격에 필수다.

| Host | 지원 OS | G3 | G4/RC | 현재 결과 |
|---|---|---:|---:|---|
| REAPER | Windows/macOS/Linux | 필수 | 필수 | NOT_RUN |
| Bitwig Studio | Windows/macOS/Linux | 필수 | 필수 | NOT_RUN |
| Logic Pro | macOS | 필수 | 필수 | NOT_RUN |
| Cubase | Windows/macOS |  | 필수 | NOT_RUN |
| Ableton Live | Windows/macOS |  | 필수 | NOT_RUN |
| Studio One | Windows/macOS |  | 필수 | NOT_RUN |
| FL Studio | Windows/macOS |  | 필수 | NOT_RUN |
| GarageBand | macOS |  | 필수 | NOT_RUN |

PASS 기록에 필요한 항목:

- 실제 OS version과 architecture
- 실제 Host 정확한 version/build
- 검증한 CLAP/VST3/AU artifact SHA-256
- scan, GUI lifecycle, state restore, transport, tempo automation, offline export, unload/reload 결과
- 실행 시각과 실행자
- raw log, screenshot, rendered audio 등 존재하는 증적 파일

체크리스트나 CI 소스만으로는 PASS를 기록할 수 없다.
