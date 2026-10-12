# Native song observation kit — 2026-10-12

The retained kit is ready for a **45–60-minute owner observation**, using a new Japanese melody and one exact installed procedural singer. `seam_singer_pilot` builds this diagnostic voice in code (`apps/seam-singer-pilot/main.cpp`), with an extended phone inventory; it is not a tuned product voice. A quality verdict applies to this recipe and synthesis path, not every voice or the whole product. Human listening, unaided use, external playback, production-bank approval and canonical Alpha/Beta acceptance remain unobserved.

The frozen code/build is `feaa3056202d8137be529baa157bef20eebf391e`. The [manifest](NATIVE_OBSERVATION_KIT_2026-10-12.json) binds the actual local files. Its outputs are ignored local artifacts; publishing this document does not upload the kit or private signing key.

## Open and inspect

From the repository root, run:

```sh
python3 tools/observation/open_native_song.py --verify-only
python3 tools/observation/open_native_song.py
```

The launcher checks the retained app bundles, public key, signed installation, project, audio and receipts before opening a fresh working copy in the rebuilt macOS editor with an isolated application root. The reference stays untouched even if the working copy is saved. The bundles and CLIs were copied byte-for-byte into the kit before further development, so a later build-directory rebuild cannot change this observation baseline. If a frozen file changes, verification refuses; retain/refreeze a new session instead of silently using different material.

The project is `out/observation/native-song-2026-10-12/Native Observation 30s.seam`. The master and one track stem are in `native-final-30s/` below that directory, with an editable exported project, recipe and receipt. The untouched source score is in `source-30s/baseline.seam`. The guide and observation form can be copied into the kit for answers without modifying frozen artifacts.

The prepared score contains 30 notes, 960 PPQ, 120 BPM, each 1920 ticks: **57,600 ticks / 30.000 seconds**. It ends on a voiced `あ`, with no final rest. Japanese lyric tokens are:

> あ さ の ひ か り を ま ど で み つ け た あ お い そ ら へ こ え を と ど け よ う あ あ

The pitches are recorded in the manifest's preparation receipt. For a composition observation, start a new project and enter an unfamiliar phrase yourself; using the prepared reference tests playback/edit/export, and must be recorded as that narrower task.

## Observation procedure

1. Start the timer. Open the prepared song, use Save As to create a working copy, identify its voice and inspect the notes. Record any help needed to understand the screen. Keep the frozen reference unchanged.
2. Select the installed `Observation Singer - engineering only`, version `0.1.0-observation`. Attempt this before reading the help section below. Record any help needed to discover the control.
3. Preview using Space. Listen for intelligibility, transitions, sustained tone, timing and objectionable artifacts. The app's Ready status and meter are engineering signals, not a listening verdict.
4. Draw one small Formant or Dynamics change, preview it, undo, and preview again. Record whether the change and undo are understandable and audible.
5. Save a **new working copy** outside the signed singer directory, close it, reopen it, and verify the expected notes/voice/edit state.
6. Use Export > Export set and a new destination. Retain master, stem and receipt. Play the WAV in an external player and report whether it is audible and matches the app. Record this separately from native playback.
7. Rank three concrete problems. Choose whether **sound quality or workflow** is the larger obstacle, with the song time/step and what you expected. The next repair uses the same material and a declared before/after criterion.

Use [the five-field form](NATIVE_OBSERVATION_FORM_2026-10-12.json): step, result, expected, observed, helpOrProblem. Results are `UNAIDED`, `HELP_NEEDED`, `BLOCKED`, or `NOT_OBSERVED`; none is prefilled as successful. Keep the session's manifest digest with your answers. Do not test recovery or remove a singer in this frozen tree; engineering failure probes use disposable copies.

## Engineering rehearsal and limits

The actual rebuilt native app selected the installed singer through its native picker and saved the project. A drawn formant point reached 9.523809432983398 semitones at document revision 5; native Undo restored no curve and zero semitones at revision 6. The final WAV is byte-identical to the pre-edit 30-second export. Native preview showed Playing with Master L/R at -42.7 dBFS, then Stopped. Native export committed 4/4 files. The CLI separately verified the signed installed resource against the saved project; track/region arguments use canonical 16-digit IDs, rather than the shorter project JSON spelling.

A fresh native process reopened the corrected saved project and retained screenshot, geometry, semantic bounds, font identity and performance evidence. Studio opened in designer mode with no microphone recording. These automated actions do not establish a human's unaided use, heard quality, screen-reader operation, or full Studio production workflow. The corrected final project's file-dialog reopen is left for owner observation.

The first preparation incorrectly assumed 480 PPQ. Its actual native WAV was 15 seconds, so it is separated under `rejected-preparation-15s/`. Only `source-30s`, `Native Observation 30s.seam` and `native-final-30s` are the final song. A second preparation error paired 31 lyric tokens with 30 pitches using Python `zip` in the agent's ad hoc command, dropping the final `pau` before invoking the CLI. The CLI received and retained exactly 30 notes; this was not a repository-tool truncation defect. The preparation receipt now records the exact 30 submitted tokens and the rejected token separately. No project JSON was manually repaired.

The -42.7 dBFS native meter value was one instant during preview. The final file's measured peak is -19.84 dBFS and RMS -36.53 dBFS. Record the monitoring level and any volume adjustment with your listening notes. No gain or sound-quality tuning was applied before observation.

Full Release rebuild passed. Three focused CTest entries passed in 28.74 seconds: original singer song journey, procedural install journey and singer-pilot CLI. Raw build/test logs and agent-transcribed native observations are bound by the manifest. The original full-product obligations remain in [the reassessment](../reviews/OVERALL_PRODUCT_BETA_REASSESSMENT_2026-10-12.md) and [execution ledger](FULL_SCOPE_BETA_EXECUTION.md).

## Help only if stuck

Using these hints means `HELP_NEEDED` for the affected step. The frozen build's native selection path is **File > Select Installed Singer**. Its visible Change voice browser lists sample banks and omits installed procedural singers: this was the rehearsal's first workflow finding. A second finding is that Singer actions > Install or relink voicebank reports that standalone installation is not connected; the existing File-menu path is separate. These are engineering findings, not a human's ranked verdict.
