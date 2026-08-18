# Mandatory Target OS and DAW Validation After Phase 12C

> **These validations are mandatory, not optional.**  
> Project SEAM must not advance to Beta, Release Candidate, or General Availability until Windows, macOS, and commercial DAW tests have been executed on the actual target operating system and host, with retained evidence.

## Result states

```text
NOT_RUN
BLOCKED
FAIL
PASS
```

Implementation state is separate:

```text
NOT_STARTED
SOURCE_READY
CI_CONFIGURED
TARGET_BUILD_PASS
```

An implementation or build state never implies runtime PASS.

## Mandatory Windows validation

Current result: `NOT_RUN`

- Windows 11 x64 target runtime
- Win32 child GUI lifecycle
- Korean and Japanese TSF/IME composition
- WASAPI input/output
- CLAP scan, load, state restore, transport, note input, and automation
- supported sample-rate and buffer matrix
- 1,000 GUI lifecycle cycles
- two-hour soak
- Authenticode signing
- clean-OS install, update, and uninstall

## Mandatory macOS validation

Current result: `NOT_RUN`

- supported Apple Silicon macOS target runtime
- Cocoa child `NSView` lifecycle
- Korean and Japanese `NSTextInputClient` composition
- CoreAudio input/output
- `.clap` bundle scan, state restore, transport, note input, and automation
- supported sample-rate and buffer matrix
- 1,000 GUI lifecycle cycles
- two-hour soak
- Developer ID signing, notarization, and stapling
- clean-OS PKG install, update, and uninstall

## Mandatory commercial hosts

| Host | OS | Current result | Beta gate | RC gate |
|---|---|---:|---:|---:|
| REAPER | Windows, macOS | NOT_RUN | required | required |
| Bitwig Studio | Windows, macOS | NOT_RUN | required | required |
| Logic Pro | macOS | NOT_RUN | required | required |
| Cubase | Windows, macOS | NOT_RUN | no | required |
| Ableton Live | Windows, macOS | NOT_RUN | no | required |
| Studio One | Windows, macOS | NOT_RUN | no | required |
| FL Studio | Windows, macOS | NOT_RUN | no | required |
| GarageBand | macOS | NOT_RUN | no | required |

Every host run must cover plugin scan, GUI lifecycle, state restore, transport, tempo automation, note events and expressions, offline export, supported channel layouts, sample-rate changes, buffer-size changes, unload/reload, and extended playback stability.

A source inspection, first-party mock host, or configured CI workflow cannot change a commercial host result to PASS.
