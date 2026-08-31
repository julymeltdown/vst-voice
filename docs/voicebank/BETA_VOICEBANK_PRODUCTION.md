# External Beta voicebank production workflow

This workflow creates and operates a recoverable Beta voicebank production project. It does not create a release voicebank by itself. The checked-in strategy assessment proves that an acquisition path is feasible; it does not prove that a performer, licence grant, recording, musical approval, or distributable bank exists.

## Truth boundary

| State | Meaning |
|---|---|
| `READY_FOR_ACQUISITION` | One source strategy has a complete rights, coverage, and listening plan. |
| `NOT_RUN` asset admission | No real source asset has passed the rights and technical admission gate. |
| `SYNTHETIC_READY_REAL_ASSETS_REQUIRED` | The U56 system can export deterministic U57 inputs from a synthetic workflow. |
| `BLOCKED` dossier | The Beta bank cannot ship until U57 supplies real lawful assets and independent approvals. |

`docs/voicebank/BETA_VOICE_SOURCE_STRATEGIES.json` compares human recording, self-authored procedural synthesis, and TTS-derived audio. The selected human-recording strategy is feasible because the contract template requires all four Beta scopes. Its `CONTRACT_TEMPLATE_READY` state is not a signed contract.

## TTS and procedural sources

A free tier or permission for commercial TTS output is not enough. A TTS-derived source can be admitted only when the exact model, service, voice, and output terms explicitly permit:

1. source-audio use;
2. transformation and segmentation;
3. redistribution as a reusable singing voicebank;
4. commercial and non-commercial end-user renders.

The checked-in TTS example is blocked because singing-bank redistribution is missing. Technical quality cannot override that missing right. A self-authored procedural source remains `NOT_ASSESSED` until its code, dependencies, absence of restricted samples or learned voice identity, coverage, and listening quality are reviewed.

## Create the deterministic inventory outputs

From the repository root:

```bash
python3 tools/voicebank-script-generator/main.py \
  --json-output build/voicebank-production/inventory.json \
  --csv-output build/voicebank-production/operator-script.csv \
  --production-assignments-output build/voicebank-production/assignments.json
```

The JSON inventory, operator CSV, and production assignments are generated from the same profile and hash. Do not edit any of them by hand.

The checked-in 72-coverage-key, 144-assignment inventory can be used directly
to verify the U56 workflow. It is a bounded production fixture, not the final
complete Japanese Beta inventory. Before real acquisition starts, U57 must
lock the music-reviewed complete inventory and create a new project bound to
that inventory hash.

```bash
python3 -m tools.external_beta.voicebank_production init-project \
  --inventory docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json \
  --strategies docs/voicebank/BETA_VOICE_SOURCE_STRATEGIES.json \
  --workspace build/voicebank-production/workspace \
  --project-id beta-voicebank-production-01 \
  --operator-id producer-01 \
  --occurred-at 2026-08-31T13:00:00Z
```

Initialization writes generation 1, its hash-bound journal event, the current project pointer, the immutable asset-store directories, and an empty staging directory as one workspace publication. Reusing a populated destination fails rather than overwriting it.

## Open Voicebank Studio before audio exists

Read `inventorySha256` from the inventory generated above, then launch the native Studio without a manifest:

```bash
./build/dev/seam_voicebank_studio_native \
  --production-project build/voicebank-production/workspace \
  --inventory-sha256 <64-character-inventory-hash> \
  --operator-id producer-01
```

The left rail shows every required coverage and pitch assignment. The center shows the selected prompt and planned take. The inspector shows the durable generation, strategy feasibility, missing/rejected/retake/review/approved queues, and incomplete staged recovery candidates.

Use Up and Down to select a required unit. Press R to start or stop recording. A stopped take is written outside the immutable store, inspected, hashed, copied into content-addressed raw storage, and journaled. Failed dry-take inspection enters `REJECTED`; it never becomes approved because recording completed.

An externally recorded PCM WAV can follow the same path without editing project JSON:

```bash
./build/dev/seam_voicebank_studio_native \
  --production-project build/voicebank-production/workspace \
  --inventory-sha256 <64-character-inventory-hash> \
  --operator-id producer-01 \
  --production-unit-index 0 \
  --import-take /secure/intake/session-01/take.wav \
  --auto-close-ms 500
```

The imported source file remains untouched. Its immutable store copy is addressed by SHA-256. Re-recording an occupied assignment creates an explicit retake that supersedes the prior take without deleting either record.

## Apply deterministic audio operations

Voicebank Studio accepts one versioned operation per invocation. The supported names are `channel-select`, `downmix`, `resample`, `remove-dc`, `normalize`, `trim`, and `segment`.

```bash
./build/dev/seam_voicebank_studio_native \
  --production-project build/voicebank-production/workspace \
  --inventory-sha256 <64-character-inventory-hash> \
  --operator-id producer-01 \
  --production-unit-index 0 \
  --operation normalize \
  --target-peak 0.8 \
  --auto-close-ms 500
```

Channel selection uses `--channel-index`. Resampling uses `--target-sample-rate`. Trim and segmentation require explicit `--start-frame` and exclusive `--end-frame` values. Every committed revision records the immutable input hash, operation name and version, parameters, output hash, operator, and UTC time. Pitch correction is not supported. Marker and pitch-mark edits remain metadata revisions and preserve the raw hash.

If interruption occurs after an output is staged but before the new generation commits, reopening the workspace recovers the last valid generation and lists the unmatched staged WAV as a recovery candidate. Committed staged bytes remain inspectable on disk but are no longer reported as incomplete recovery work.

## Attach a candidate manifest for marker work

Once a candidate manifest and its referenced audio exist, open both surfaces together:

```bash
./build/dev/seam_voicebank_studio_native \
  --manifest /secure/production/candidate/manifest.json \
  --production-project build/voicebank-production/workspace \
  --inventory-sha256 <64-character-inventory-hash> \
  --operator-id producer-01
```

Marker or pitch-mark edits followed by Ctrl+S create a metadata revision bound to the take and immutable raw hash. The manifest save and production journal must both succeed before Studio reports `SAVED`.

## Export the U57 handoff

```bash
./build/dev/seam_voicebank_studio_native \
  --production-project build/voicebank-production/workspace \
  --inventory-sha256 <64-character-inventory-hash> \
  --operator-id producer-01 \
  --export-u57-inputs build/voicebank-production/u57-inputs \
  --auto-close-ms 500
```

The output contains `production-brief.json` and `candidate-template.json`. The candidate template stays `BLOCKED` with real assets, independent rights approval, and independent musical approval marked `NOT_RUN`.

Validate every durable generation, journal binding, inventory assignment, licence hash, and immutable asset hash:

```bash
python3 -m tools.external_beta.voicebank_production validate-workspace \
  --inventory docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json \
  --strategies docs/voicebank/BETA_VOICE_SOURCE_STRATEGIES.json \
  --workspace build/voicebank-production/workspace
```

U56 is complete when this synthetic workflow passes without hand-edited JSON or CSV. U57 remains responsible for real lawful audio, complete coverage, retake closure, marker and pitch review, renderer listening, and independent rights and musical approval. Only U57 evidence can unblock `docs/voicebank/beta-voicebank-01-dossier.json`.
