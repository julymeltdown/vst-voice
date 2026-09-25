# Native SING shell captures

Software-raster frames from `Project SEAM.app --screenshot` at build `b11daf7e` (Release, macOS
arm64, device scale 2). Each PNG is the @2x frame downsampled to logical size. They show what the
native code paints. They are **not** FL Studio host captures, and they are not a pixel-match
verdict against the concept images.

| File | Mode | Logical size | Project |
|---|---|---|---|
| `native-emo-sing-1600x900@2x-b11daf7e.png` | EMO | 1600×900 | `tests/fixtures/projects/schema-7-historical-writer.seam` (voicebank missing) |
| `native-scene-sing-1600x900@2x-b11daf7e.png` | SCENE | 1600×900 | same |
| `native-emo-sing-1280x800@2x-b11daf7e.png` | EMO | 1280×800 (CLAP default) | same |
| `native-scene-sing-rail-1000x700@2x-b11daf7e.png` | SCENE | 1000×700 (rack collapsed to rail) | same |
| `native-emo-sing-empty-1600x900@2x-b11daf7e.png` | EMO | 1600×900 | new project, procedural draft singer |
| `native-scene-sing-1600x900@2x-7e7952f2.png` | SCENE | 1600×900 | schema-7 fixture, after the review fixes |
| `native-scene-sing-1280x480@2x-7e7952f2.png` | SCENE | 1280×480 (short: rail rack, compact lane) | same |
| `native-emo-sing-720x480@2x-7e7952f2.png` | EMO | 720×480 (narrow + short: tabs dropped, notes-below hint) | same |

Reproduce: `SEAM_UI_DESIGN=emo "build/release/Project SEAM.app/Contents/MacOS/Project SEAM"
--auto-close-ms 3000 --screenshot out.ppm --window-width 1600 --window-height 900
--application-support-root <temp dir> --force-threaded-audio <copy of project>`.
