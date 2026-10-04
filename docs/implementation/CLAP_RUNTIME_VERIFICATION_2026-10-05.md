# CLAP runtime verification, 2026-10-05

Why this entry exists: the preceding entries verified SEAM's **codec and CLI**
paths, which is what an import workflow depends on. It says nothing about the
bundle a DAW actually loads. FL Studio does not run `seam_voicebank_cli`; it
loads `ProjectSEAM.clap`. That surface had not been exercised in this campaign,
so it was checked directly rather than assumed.

## What the plug-in is

`ProjectSEAM.clap` identifies itself as **`com.project-seam.render-player`** — a
**render player**, not a live synthesiser. It links only `seam_clap` and
`seam_core` (`CMakeLists.txt:1149`); it does not link the rendering or
interchange libraries, which is why rebuilding it after this week's interchange
changes correctly reported *no work to do*.

Its state format takes **audio, not a score**: `seam_clap_state_tool pack
INPUT.wav OUTPUT.seamclapstate`. Synthesis happens offline through the editor or
CLI, and the plug-in plays the result with host transport and gain automation.
This is the documented Phase 10 design (`docs/architecture/CLAP_PLUGIN_RUNTIME.md`,
README line 20), not a defect.

## Verified on the real bundle

Built from current HEAD and driven through the project's own host harness with a
state packed from a **real render produced this week** (the two-track kana MIDI
import from the previous entry), not a synthetic tone:

```
CLAP smoke PASS: Project SEAM Render Player, 1 channels
{
  "pluginId": "com.project-seam.render-player",
  "channels": 1,
  "framesProcessed": 256,
  "automationRatio": 0.501187,
  "stateRoundTrip": true,
  "activeLoadRejected": true,
  "restartRequests": 1,
  "transportPauseSilence": true
}
```

**The smoke test does prove audio flows, and this was checked rather than
assumed.** `automationRatio` averages `automated / reference` over every frame
where the reference is non-zero. Total silence would make that ratio `0.0`, and
`abs(0.0 - 0.501187) > 0.01` fails the host (`apps/seam-clap-host/main.cpp:278`).
The observed `0.501187` matches `10^(-6/20)` exactly, so the plug-in emitted
real samples and applied the −6 dB Master Gain correctly.

`transportPauseSilence: true` confirms the complementary case: a paused
transport yields exact zeros.

All 11 CLAP tests pass, including `seam_clap_plugin_host_smoke`,
`seam_clap_state_pack_smoke`, `seam_clap_state_extract_smoke` and
`seam_clap_authoring_adapter_contract`.

`ProjectSEAMEditor.clap` (Mach-O arm64 bundle, 12 MB) is present for the
DAW-embedded editor surface.

## What this means for the FL Studio test pass

The plug-in is healthy and its audio path is proven. Two boundaries are worth
stating before a session is scheduled, because both are easy to mistake for
bugs:

1. **The plug-in plays rendered audio; it does not synthesise.** Import a
   `.mid`/`.ustx` and render it through the editor or CLI first, then load the
   resulting render into the plug-in. Expecting live synthesis from the DAW will
   look like a silent plug-in when it is working as designed.
2. **Editor surface coverage is platform-bounded.** README row 5 records that
   the editor draws with a macOS vector backend; a build without it shows
   `paintEditorUnavailable` and accepts no pointer or key input. This is a TODO,
   not a regression, and it does not affect macOS.

## What is not claimed

No DAW has been driven. `seam_clap_host` is SEAM's own harness, not FL Studio,
Reaper or Bitwig. Nothing here is listening evidence, screen-reader evidence or
host-matrix evidence, and none of it substitutes for the human sessions the Beta
gate requires. P0-08 stays **OPEN**.
