# Project SEAM

Project SEAM is a native C++20 singing-voice editor for Apple Silicon Macs. It sings scores through three source families — recorded sample banks, authored procedural singer recipes, and an admitted neural execution path — and treats phoneme boundaries, source-unit choices, pitch-shift artifacts and sample seams as editable musical material rather than defects to hide.

**Status on 2026-10-18: a working engineering prototype, not yet a usable or releasable product.** The SING editor, the sample and procedural renderers, project save/reopen/recovery, signed voicebank packaging and installation, and transactional export are implemented and exercised by automated tests. No product gate has passed: [Usable Alpha](docs/product/USABLE_ALPHA_ACCEPTANCE.md) is 0/20 (all `NOT_RUN`), External Beta is `BLOCKED`, and no voicebank has been released. The binding blockers are a rights-cleared production voicebank, the owner's own listening and workflow judgment, and the unaided user journey. [Current status](docs/STATUS.md) gives the evidence; the [completion plan](docs/plans/2026-10-18-project-completion-plan.md) gives the order of work.

## Start here

| You want to | Read |
|---|---|
| Know what is true today, with the checks behind it | [docs/STATUS.md](docs/STATUS.md) |
| Decide what to build or decide next | [Completion plan](docs/plans/2026-10-18-project-completion-plan.md) |
| Find the code that owns a behavior or API | [Current engineering entrypoints](docs/implementation/CURRENT_ENGINEERING_ENTRYPOINTS.md) |
| See how the libraries depend on each other | [Architecture overview](docs/architecture/OVERVIEW.md) |
| Know what "done" means for a user | [Usable Alpha contract](docs/product/USABLE_ALPHA_ACCEPTANCE.md), [full Beta contract](docs/product/full-product-beta-contract.json) |
| Work on the editor's look | [DESIGN.md](DESIGN.md) |
| Find any other document, or a retired one | [Documentation map](docs/README.md) |
| Work in this repository as an agent | [AGENTS.md](AGENTS.md) |

## Platform scope: macOS arm64 is the active target

**Windows support is a TODO.** It is deferred, not abandoned and not failed. The developer currently has macOS only, so Windows work must not hold up macOS singer development; resume Windows-specific implementation and verification when a suitable Windows environment is available.

- **macOS arm64 (Apple M3 Max) is the only development, verification and release target right now.**
- **Windows x64 stays a declared contract platform.** [The full Beta contract](docs/product/full-product-beta-contract.json) requires two platforms and nine host tuples, four of them on Windows, so **Beta GO is not reachable while Windows is deferred.** This is a schedule fact, not a scope reduction; no requirement has been deleted or relaxed.
- **GitHub orchestration is deferred.** Local source checks report `SOURCE_READY`; target runtime, signing and clean-OS evidence remain separate obligations.

### Windows TODO

| # | Item | State |
|---|---|---|
| 1 | Historical `windows-installer-smoke` certificate-trust hang | Open. Before workflow removal, Import-Certificate, .NET `X509Store` and `certutil -addstore` without `-f` were reported hanging at the signing-identity step. Reproduce on an actual Windows host before selecting a repair; `certutil -addstore -f` remains unverified. |
| 2 | Windows clean-install / same-version reinstall / uninstall lifecycle | Never executed. Blocked by item 1. |
| 3 | Windows REAPER and Bitwig host tuples (CLAP and VST3) | Never executed. Needs a Windows host, not just CI. |
| 4 | Windows x64 native pitch and acoustic acceptance | Not measured. All current measurements are macOS. |
| 5 | Windows (and Linux) editor surface | Not implemented. The only editor is the SING shell, drawn with the macOS CoreGraphics/CoreText vector backend. A build without that backend still compiles its source contracts, Win32/X11 windows and accessibility bridges, but its standalone window and CLAP view show only an unavailable-editor notice (`paintEditorUnavailable`); pointer input does nothing and keys do not edit the score. Resuming needs a vector backend for the platform. |

**Prerequisite to resuming Windows:** a Windows host (physical or cloud) able to run the installer lifecycle and a DAW, with an interactive session for the certificate-trust step.

## Build, run and test

Prerequisites: CMake 3.25+, Ninja, a C++20 Apple toolchain with the macOS SDK, and a discoverable OpenSSL 3 Crypto. The optional production neural worker also needs ONNX Runtime and native ONNX schema inputs; see [the runtime guide](tools/neural_runtime/README.md).

```sh
cmake --preset release
cmake --build --preset release -j6
open "build/release/Project SEAM.app"
ctest --preset release
```

`ctest --preset release -L '^unit$'` runs one tier (`unit`, `journey` or `contract`), and `-LE '^slow$'` skips the entries that take 5 s or more. [STATUS](docs/STATUS.md#test-tiers) lists the measured time of each tier.

The `dev` (Debug), `sanitize` (ASan + UBSan) and `thread-sanitize` presets use the same pattern and build into `build/<preset>`. All presets treat warnings as errors.

Verification hygiene this repository has paid for:

- After any change under `libs/`, run a full `cmake --build build/release -j6` before trusting CTest. A focused `--target` build leaves other binaries linked against the old library, and the stale binary fails in a way that points at the newest change.
- Check artifact timestamps before treating a failure as real.
- Sanitizer runs use `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:strict_string_checks=1` and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`.
- A passing module test, a `*_CONTRACT=PASS` line or a source-ready gate is engineering evidence only. It never establishes human listening, DAW, VoiceOver, signing, Windows or reviewer acceptance.

### Giving the editor a singer

A fresh build ships no singer, so a new project shows *No singer selected* and cannot play. Two working routes exist today; neither is a released voicebank.

- **Japanese sample bank for engineering (recorded human voice, male, CC BY-SA 4.0, never bundle it).** Build it from the PJS 1.1 corpus with [pjs_engineering_bank.py](tools/voicebank_source_intake/README.md#engineering-only-japanese-bank-from-pjs-11), then sign and install it with a local key. A command-line install records a signer-trusted receipt, so the bank is selectable when the app is launched from Finder. The in-app installer only accepts banks signed by a key passed with `--trusted-voicebank-key`, which a Finder launch never does.

  ```sh
  build/release/seam_bank_tool keygen KEYS/private.json KEYS/public.json
  build/release/seam_bank_tool pack BANK_DIR BANK.seambank KEYS/private.json
  build/release/seam_bank_tool install BANK.seambank "$HOME/Library/Application Support/ProjectSEAM/Voicebanks" --public-key KEYS/public.json
  ```

  `seam_voicebank_cli export-installed-bank-project PROJECT.seam OUT.wav VOICEBANK_ROOT` renders a project pinned to an installed bank without the GUI.
- **Procedural singer from Voicebank Studio.** Design a voice in Studio, publish it and choose *Install published singer* (Command-Option-I). It sings, but its quality is unqualified, and Usable Alpha requires a sample-concatenative bank.

## What you get after a build

| Product | CMake target | Role |
|---|---|---|
| `Project SEAM.app` | `seam_editor_native` | The standalone SING editor (EMO and SCENE looks): notes, lyrics, phoneme/unit/seam lanes, pitch and expression curves, transport, save/reopen/autosave recovery, voicebank selection and relink, master and stem export. |
| `SEAM Voicebank Studio.app` | `seam_voicebank_studio_native` | Producer workspace: recording and import, take review, prepared-donor intake, candidate packaging. |
| `ProjectSEAM.clap`, `ProjectSEAMEditor.clap` | `seam_clap_plugin`, `seam_clap_editor_plugin` | CLAP render player and DAW-embedded editor. VST3/AUv2 projections come from the Phase 13A pipeline. |
| `seam_voicebank_cli`, `seam_bank_tool` | same | Bank validation, packaging, signing, installation, score import/export (USTX, SMF) and engineering operations. |
| `seam_singer_pilot` | same | Positional diagnostic writer. Read [its source](apps/seam-singer-pilot/main.cpp) before running it; its musical output is unqualified. |
| `seam_neural_worker` | same | Optional out-of-process neural worker (protocol 3). Only arithmetic fixtures run today; no learned singer is qualified. |
| `seam_fixture_*`, `seam_smoke_*` | same | Test inputs and smoke journeys, not product surfaces. `seam_fixture_synthetic_bank` and `seam_fixture_clap_state_wav` write the synthetic voicebank and CLAP state WAV that later tests read; the nine `seam_smoke_*` programs each run one pipeline end to end (PSOLA render, editing and render cache, native editor capture, four-channel routing, signed bank install, platform capabilities, CJK text raster, production preview, multi-track routing). |

## Repository layout

| Path | Contents |
|---|---|
| `libs/` | 25 product libraries: domain model, formats, phonemizer, voicebank, synthesis, rendering, authoring runtime, native UI, platform adapters, CLAP, distribution, neural synthesis and producer workflow. See the [architecture overview](docs/architecture/OVERVIEW.md). |
| `apps/` | Executable entry points listed above. |
| `phase12c/`, `phase13a/`, `phase13b/` | Live-voice runtime and validation harnesses, VST3/AUv2/installer/signing gates, and content/IP release gates. The names are historical; the code is current. |
| `tests/` | Native doctest suites, Python contract tests and fixtures. |
| `tools/`, `scripts/` | Python tooling: release gates, evidence collectors, training experiments, source intake, observation kits, packaging and signing scripts. |
| `assets/` | Development fixtures, fonts, UI design assets and Character 01 development art. Nothing here is a released voicebank. |
| `packaging/` | Installer, bundle and trust metadata. |
| `docs/` | Contracts, formats, ADRs, design, legal requirements and dated engineering records. Start at the [documentation map](docs/README.md). |

## Release engineering gates

Release gates are implemented and fail closed. Until real target evidence exists they are expected to report blocked:

```sh
python3 -B scripts/verify_usable_alpha_contract.py --root .
python3 -B -m tools.external_beta.voicebank_gate --dossier docs/voicebank/beta-voicebank-01-dossier.json --root .
python3 scripts/verify_phase13a_contracts.py --root .
python3 tools/phase13a/release_gate.py check --matrix docs/phase13a/mandatory-validation-matrix.json --gate G4
```

The first prints `USABLE_ALPHA_CONTRACT=PASS` when the contract record is valid; that line does not mean any requirement passed. VST3 validators, `auval`, Windows Authenticode, Apple notarization, clean-OS installer tests and commercial DAWs are mandatory and remain `NOT_RUN`.

## Repository policy

Only `master` is permitted. Enable the hooks with `git config core.hooksPath .githooks`. Commit and push cadence, verification hygiene and evidence-honesty rules are in [AGENTS.md](AGENTS.md). Do not modify `.github`; CI work is deferred.

## Ownership and licensing

Project source and first-party concept assets are proprietary and all rights are reserved. Distributed third-party source and assets are limited to the audited permissive and public-domain entries in `third_party/manifest.yml`; behavioral references are not vendored. See [LICENSE](LICENSE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [third_party/manifest.yml](third_party/manifest.yml).
