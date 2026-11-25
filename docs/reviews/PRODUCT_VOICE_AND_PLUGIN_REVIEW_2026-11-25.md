# Project SEAM product, voice and plug-in review

25 November 2026. Baseline `72cda3a5` on macOS arm64. The reference task is to insert one instrument, enter Japanese lyrics against notes, audition a voice the owner wants to use, adjust a join and reopen the song with the same sound.

## How far the project has come

| Question | Evidence at the baseline | Product consequence |
|---|---|---|
| Can the engine sing the observation score? | The October installed-bank run exported 30 seconds of PJS audio, voiced all 30 notes, put 29 within 50 cents and reproduced the export bytes. The November scan records a successful installed-bank launch and render. | Someone with this engineering bank can test song editing and export today. |
| Can a new installation sing the same score? | The clean-root launch recorded an unresolved bank, idle render and unavailable transport. The application ships with no singer. | This breaks the first session before the creator can judge editing quality. |
| Does the sound fit the brief? | The owner wants a more feminine timbre. The current observation voice uses 1,696 recorded PJS units. All nine local audition WAV headers and hashes were checked for this review. | The current voice misses the owner's direction. Preference between the alternatives is open. |
| Can a musician complete the intended journey? | The Alpha contract verifier passes its schema check. The acceptance record has 20 requirements and zero completed observations. | Workflow rehearsals support engineering decisions. An unaided user session with a cleared bank still needs recording. |
| Can a producer distribute it? | The Beta dossier verifier returns 52 errors across 12 categories, including rights, coverage, listening, signed package, clean install and reference song. External Beta has nine requirements and an empty evidence array. | A successful render with a development key cannot answer these questions. |

Measured gate completion is **0% for recorded Usable Alpha acceptance (0/20) and 0% for External Beta acceptance (0/9)**. The whole project has no defensible percentage of work complete. Counting 83 product cases, 18 work packages, commits or passing CTests weights engineering effort differently from a performer grant or an unaided session. The functional distinction matters when allocating the next week. Editing and rendering already support an experiment whose result can change the product. [Functional observations](../plans/FUNCTIONAL_READINESS.md) · [Alpha record](../product/usable-alpha-acceptance.json) · [Beta record](../product/external-beta-acceptance.json) · [Full contract](../product/full-product-beta-contract.json)

## Where the development time goes

A note edit passes through the document, selection, snapshot, renderer, audio transport and host state. Those boundaries earn their cost when a project reopens with the exact singer and an old render cannot replace a newer one. The production bank has a separate dependency chain from performer and approved intended use through labeled recordings, unit selection, signed installation and listening review. More coverage of the first chain produces little evidence about the second. The scheduled scan still names an outstanding owner verdict. Record the owner's timbre objection as direction, and keep formal listening and observation rows open until a particular file and session earn them. [Status](../STATUS.md) · [Completion decisions D3–D8](../plans/2026-10-18-project-completion-plan.md)

## Competitive position

| Creator task | Commercial products | OpenUtau | Consequence for SEAM |
|---|---|---|---|
| Start with an appealing singer | Yamaha offers a voicebank catalogue with previews. Dreamtonics sells voices for its offline neural editor. | The getting-started workflow installs a singer and selects a suitable phonemizer and renderer. Existing compatible singers give creators a choice. | The engineering bank demonstrates the pipeline. A clearly identified first singer and a short playable phrase would change first use more than another editor tab. |
| Shape a performance | Synthesizer V Studio 2 Pro exposes pitch, phonemes, expression and timbre, with voice-to-MIDI and ARA. VOCALOID7 combines AI-assisted parameter generation with editing. | The phonemizer and renderer influence the available editing workflow. Existing UTAU users bring voice-specific pronunciation and tuning experience. | Pitch, vibrato, dynamics and retiming already have editor paths. The strongest demonstration would show exactly which recorded unit changed, where two units meet and how the change sounds. |
| Reuse a song or bank | Commercial ecosystems provide their own supported voices and host integrations. Offline synthesis is also available commercially. | The USTX project format and voicebank installation are distinct parts of the workflow. | SEAM supports USTX/MIDI interchange. Importing a score still requires a compatible SEAM resource. A personal-use bank importer would need alias, timing, sample-pitch and missing-unit handling before it could close that gap. |
| Finish inside a DAW | Dreamtonics offers both standalone and DAW operation. | OpenUtau's published introduction centers on its singing editor and renderer ecosystem. | SEAM's native CLAP editor and pinned VST3/AUv2 wrapper path offer a focused instrument workflow. Host recall, resize, playback and reopen still require target-host evidence. |
| Repeat and inspect a render | The vendors describe expressive editing and rendered results. | Extensible renderers and third-party banks increase choice and the number of possible setups. | Exact resource relink, deterministic render identity, explicit seam overrides and data-only signed banks are valuable for archival recall and deliberate concatenative sounds. The cost is producing and admitting a bank for the first song. |

Feature comparison uses published capabilities. A matched, level-controlled listening comparison across products remains outstanding. The highest-value benchmark is one identical Japanese phrase, one deliberate note-boundary change and one save/reopen cycle. Record setup time, lyric corrections, audible seam defects and whether the same sound returns. [Yamaha VOCALOID](https://www.vocaloid.com/en/) · [Synthesizer V Studio 2 Pro](https://dreamtonics.com/synthesizerv/) · [OpenUtau](https://github.com/openutau/OpenUtau) · [OpenUtau getting started](https://github.com/openutau/OpenUtau/wiki/Getting-Started) · [SEAM wrapper architecture](../phase11/VST3_AU_WRAPPER.md)

## One instrument faceplate

The owner's correction sets the hierarchy. The host owns the window. SEAM gets one printed faceplate. A sky-blue ink band and a heavy wordmark carry the identity. Immediately below, one strip holds the workspaces and transport. The piano roll owns most of the area. A red playhead and pitch trace stay legible over white paper and blue note blocks. A reader should be able to follow the same time position down through lyric, phoneme and unit boundary. The right column identifies the actual singer and shows controls the active renderer can apply.

| Visual reference | Application | Acceptance check |
|---|---|---|
| Sky and shallow hardware chrome | Restrict sky texture to the masthead. Use square inset readouts and thin printed rules. | A cropped editor at host size still reads as one instrument. |
| Cerulean halftone print and two-color posters | Use a restrained blue dot screen, ink lettering and a red timing accent. | Paused and selected notes, focus and warning states remain distinguishable in high contrast. |
| Graphic score and ASCII sequences | Align unit blocks, boundary ticks and phonemes to real score time. Show note names and control values in legible type. | Zoom, scroll, edit and undo preserve the link between the displayed join and its source event. |
| Zine and metallic portable-player references | Use type hierarchy and an inset singer display. Keep decoration outside data and avoid animation over lyrics. | At 1280 × 800 a creator can identify the singer, start playback and read the lyrics without opening a second surface. |

The [concept image](../design/concepts/concept-cerulean-plugin-2026-11-23.png) sets composition and print character. Its example lyrics, pitches, controls and status text are illustrative. Runtime labels must come from project and renderer state. Implement in the shared native shell so the plug-in and standalone use the same controls and semantic bounds. Inspect the native frame at host size, then verify compact layout, keyboard activation, saved appearance and high contrast. Keep host qualification separate from shell captures. [Native shell](../../libs/seam-native-ui/src/design/sing_shell.cpp) · [Layout](../../libs/seam-native-ui/src/design/sing_layout.cpp)

## What the voice audition can decide

The current singer is a recorded bank built by the PJS intake tool. Changing the score's key raises its fundamental frequency and changes the melody's register. Shifting the spectral envelope at the original key isolates a timbre choice. The raw-bank renderer supports pitch, timing, dynamics, vibrato and envelopes. The current capability table gives formant control to SpectralClassic and coupled gender control to the procedural carrier. Showing all six knobs as usable on a raw PJS voice would promise changes the renderer cannot deliver. [PJS intake](../../tools/voicebank_source_intake/pjs_engineering_bank.py) · [Renderer capabilities](../../libs/seam-synthesis/src/renderer_capabilities.cpp)

| Comparison in the local listening packet | Median F0 | Envelope centroid below 5 kHz | Decision |
|---|---:|---:|---|
| 1a original → 1b, envelope +12% | 329.8 → 329.3 Hz | 1209 → 1322 Hz | First listen. Does changing the color at the written pitch move the recorded voice in the right direction? |
| 1c → 1d, higher formants and key | 391.4 → 439.2 Hz | 1372 → 1396 Hz | Both variables change. Use as upper bounds, then repeat at the original key if preferred. |
| 2a diagnostic → 2b revised recipe | 329.6 → 329.6 Hz | 273 → 295 Hz | Second listen. Judge the constructed singer's vowel shape, consonants and sustained tone independently. |
| 2c higher key, 2d original with maximum gender, 2e revised with half gender | 415.2 / 329.6 / 329.6 Hz | 361 / 295 / 336 Hz | Check register dependence and whether coupling tract shift with source tilt improves the phrase. |

These measurements identify changed signals. The owner decides whether the sound reads as feminine and whether it suits the song. Peak levels differ between the two families. Listen within each family first, then match perceived level for cross-family comparisons. Mark a timestamp and a reason for each objection, especially softened consonants, unstable vowels, metallic sustain or abrupt joins. The audition files are engineering assets under `out/listening/female-tone-2026-11-23/`. Their source terms and product bank admission are separate decisions.

## Development order and exit evidence

1. **Land one recognizable instrument surface.** Use real transport, note, singer and expression state. Exercise the embedded editor, compare a populated and an empty project, and preserve existing appearance preferences.
2. **Choose the sound with the same phrase.** Start with 1b and 2b against their references. Record preference, timestamp and whether a change of key is acceptable. A preference for neither justifies trying a different source singer before extending DSP.
3. **Prove the preferred direction in the actual project renderer.** Bind a distinct bank or recipe identity. Render the same project twice, save and reopen, confirm the score's original key and compare phoneme intelligibility at low and high notes. A post-processed WAV alone cannot establish this behavior.
4. **Admit a real product voice.** Choose a performer and record permission for source use, transformation, bank distribution and end-user output. Assign a producer and a distinct reviewer. Establish Japanese CV coverage, joins, pitch range and a reference-song verdict before bundling a bank. D3 remains an owner choice.
5. **Run a clean first session and host recall.** Install the candidate, insert the instrument, enter a lyric, play, edit a seam, export, close and reopen in the target DAW. Attach the bank hash, project, render and host version. Run the signed package and platform gates against that candidate. Advance the corresponding acceptance rows only with those records.
