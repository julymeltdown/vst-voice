# Plug-in host certification matrix

| Host or validator | Platform | Status | Evidence |
|---|---|---:|---|
| Project SEAM dynamic CLAP host | Linux/X11 | PASS | lifecycle, GUI, note input, state, transport and screenshot |
| Project SEAM dynamic CLAP host | Windows | SOURCE_READY | Win32 CI runner required |
| Project SEAM dynamic CLAP host | macOS | SOURCE_READY | Cocoa CI runner required |
| official clap-validator 0.4.1 | Linux | LOCAL_NOT_RUN / CI_CONFIGURED | local binary unavailable; pinned official CI job is configured |
| REAPER | Linux/Windows/macOS | NOT_RUN | actual host binary and version required |
| Bitwig Studio | Linux/Windows/macOS | NOT_RUN | actual host binary and version required |
| Cubase | Windows/macOS | NOT_RUN | actual host binary and version required |
| Ableton Live | Windows/macOS | NOT_RUN | actual host binary and version required |
| Studio One | Windows/macOS | NOT_RUN | actual host binary and version required |
| FL Studio | Windows/macOS | NOT_RUN | actual host binary and version required |
| Logic Pro / `auval` | macOS | NOT_RUN | AU build and actual macOS host required |

A row may move to PASS only when the exact host version, OS, architecture, sample rates, buffer sizes, GUI lifecycle, state restore, seek/loop and offline render evidence are attached.
