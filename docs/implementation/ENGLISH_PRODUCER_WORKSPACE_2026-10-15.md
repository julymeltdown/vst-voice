# English producer workspace: explicit recording contract

## Change and boundary

At source baseline `4087dbcc925167e92d140c76131b74e41fe35c76`, an actual
`new-producer-workspace` call with `language: en` refused creation: “Japanese
rules cannot stand in for other languages”. Both native and Python generators
were Japanese-only. This prevented a normal English producer journey after the
female SLT source acquisition and unassigned phone-donor preparation.

This unit implements English inventory generation, exact validation, empty
producer creation and reopening. Studio's existing Create producer workspace
action now offers a full English preset. It initializes no source, take, marker
review, pitch review, quality decision or release approval. The real producer and
distinct reviewer remain unregistered; the retained workspace uses the clearly
labelled `engineering-fixture-producer` identity for execution evidence only.

## Code contract

`libs/seam-voicebank-production/src/draft_inventory.cpp` builds the English
preset from the phonemizer's actual frozen vocabulary. The independent Python
definition in `tools/voicebank_script_generator/english_profile.py` is checked
against native generation through full-document and CSV parity. The hash binds
`seam-en-arpabet-v1`, a newline, each of its ordered 93 symbols and a final newline.

Every English inventory carries exactly these `languageContract` members:
`vocabularyId`, `vocabularySha256`, and `stressPolicy`. Minimal English input
profiles receive explicit English defaults before optional overrides. Persisted
English inventories must contain the contract. The generator version is
`seam-draft-inventory-2.1.0-en`. Japanese inventories retain their old version,
serialization and hashes and cannot carry an English contract. The schema-1
generator remains Japanese-only. Korean producer-inventory generation remains
unsupported by these generators.

The full English preset uses 17 bare vowels, 24 consonants and `pau`, with
`FOLD_LEXICAL_STRESS`. It requests sustain, release, special, CV, VC and VV
sequences. Release ends in `pau`; Japanese `R`, `br` and `glottal` tokens are not
substituted into English. Isolated consonants and pause use the existing special
unit kind. Breath and glottal performance articulations need a separate English
contract before they can be requested here. “Full” names this vocabulary preset;
it does not prove the full product's performance coverage.

Captured custom profiles can declare `DISTINGUISH_LEXICAL_STRESS` and explicitly
choose frozen stress forms. Folded inventories reject stress-suffixed vowels.
Both implementations reject foreign phone classes, raw source `jh` (SEAM uses
`j`), unsupported contracts/kinds and prior range/PASS fields. Exact deterministic
reconstruction rejects edited rows even when their inventory and CSV hashes are
recomputed. Changing a valid profile and regenerating it creates a different
inventory; the contract is an integrity binding, not an authenticated approval.

Native producer projects bind the resulting inventory hash and preserve language
`en`; they do not copy the inventory contract into a new project field. The
English sample-manifest/resource path still needs explicit contract propagation
and coverage review before a bank can be promoted. This unit does not claim to
complete that downstream integration.

## Operator path

In Studio, choose Create producer workspace and select **Full English draft**.
The preset requests MIDI layers 60, 66 and 72, neutral style, and two alternate
takes per sequence. All requested layers and range remain `NOT_ASSESSED`.

For the CLI, save an explicit profile such as:

```json
{"profileId":"my-english-voice","language":"en"}
```

Hash its exact bytes and run the existing command with a new absolute folder:

```text
seam_voicebank_cli new-producer-workspace DESTINATION PROJECT_ID PRODUCER UTC PROFILE_JSON PROFILE_SHA256
```

It creates `inventory.json`, `recording-script.csv` and `producer/`. The recording
script uses the declared phone sequences; a source's automatic labels are not
recording instructions or approved mappings. Register actual sources and a
separate reviewer through the existing production workflow after the owner's
rights/policy decision. This unit neither imports SLT nor rewrites its labels.

## Execution evidence

The retained actual full-English CLI workspace has:

| Result | Observed value |
|---|---|
| Required sequences | 1,147 |
| Planned alternate takes | 6,882 |
| Assignment rows | 3,441, all `MISSING` and unreviewed |
| Native/Python JSON and CSV | Identical |
| External draft-workspace validator | PASS |
| Sources, takes and reviews | Empty |
| Range / release | `NOT_ASSESSED` / ineligible |
| Inventory SHA-256 | `50612c318eba505e0e1611c0162987aab10880fd8c611262c40659272f35c6ee` |
| Script SHA-256 | `6e5585339a5a3736c5b92ffcdbb3a5002516175dc2819a55c7512445a2607b46` |

The full Release rebuild completed before CTest. Six focused CTest lanes passed:
138 native cases and 17 Python methods. A separate parity run passed 9 Python
methods against the rebuilt native CLI. Coverage includes English full/custom
profiles, stress and vocabulary refusals, Japanese default/custom/starter parity,
empty workspace recovery and Studio's actual app action plus controller reopen.
Studio's OS dialog and audio device were scripted; this is not physical AppKit,
microphone, VoiceOver or human acceptance.

The first rebuild failed because the new Studio test lacked the controller
header; the corrected full rebuild and final rebuild passed. A first capture
attempt could not launch its copied binary because executable mode was missing;
it executed no native command. The corrected capture retained the executing CLI
and passed. Failed logs and the initial capture script are preserved. The final
build/test receipts bind all 14 changed implementation/test files before and
after their runs, plus six binary bindings during tests. Only the actual CLI is
retained; those receipts are not a complete hermetic build or replay of every
test executable.

The [evidence index](ENGLISH_PRODUCER_WORKSPACE_2026-10-15.evidence.json) binds the
local root `out/evidence/english-producer-workspace-2026-10-15`. Its read-only
verifier checks retained hashes, immutable Git source bindings, receipts,
JSON/CSV identities and the empty draft. It does not execute native binaries or
the capture script. Local hashes establish consistency, not third-party
authentication.

## Remaining production work

The original-female source census remains 1,429 WAVs. The 1,132 SLT files are
read speech with automatic, uncorrected labels; the 312 derived phone donors do
not add original material. Stress folding in this preset does not approve an
SLT alias, create the missing `axr`, qualify stable singing spans or establish
three musical pitch layers. Source attribution, the owner's rights/public-license
policy, actual producer and distinct reviewer, reviewed boundaries, listening,
sample-manifest/resource contract integration and full singing/style coverage
remain necessary. Internet recordings do not meet AE1/R3's no-recording original
singer requirement. Classical/neural resources, multilingual product scope and
R9 remain open.

Usable Alpha remains **0/20 recorded passes**. U45's reconciliation hold and
canonical acoustic tolerance work remain open. External Beta remains **BLOCKED**.
Independent review of this exact implementation is pending.
