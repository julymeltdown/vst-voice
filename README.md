# Project SEAM — Phase 1

Project SEAM is a C++20 foundation for a sample-concatenative singing-voice editor. The product is designed around a deliberate constraint: phoneme and sample boundaries are editable musical material rather than defects to be hidden.

This repository contains the completed **Phase 1 editor foundation** on the **`master` branch only**.

## Phase 1 delivered

- Integer musical time (`Tick`) with tempo and meter maps.
- Tick, second, and sample-frame conversion.
- Canonical project model: project, vocal/audio tracks, regions, notes, lyrics, voicebank and character references.
- Strong typed IDs and post-load ID synchronization.
- Command-based editing with undo/redo and monotonic project revision.
- Add, move, resize, box-select, and delete note operations.
- Snap/quantize behavior for positive and negative musical time.
- Piano-roll viewport model with zoom, pan, hit testing, piano keyboard rows, and 10,000-note virtualization.
- Backend-independent SVG proof renderer for the editor scene.
- UTF-8-aware project serialization with an in-house JSON parser and staged temporary-file replacement.
- Real-time-safe audio callback contract plus a deterministic callback simulator.
- Test suite, benchmark, sanitizer preset, CI policy, license audit, and master-only branch enforcement.
- Three retained low-poly character directions, 128 px silhouette tests, and generated blockout OBJ fixtures.

## Deliberate Phase 1 boundary

The canonical editor and interaction model is complete for Phase 1, but the production **iPlug2 + Skia native window adapter is not vendored or compiled in this repository**. It remains dependency-gated until exact source revisions and the resulting build closure pass the project license audit. The current proof renderer writes the same visible editor model to SVG, so application logic and UI geometry can be tested without coupling the domain to a graphics SDK.

This is not a complete singing synthesizer yet. Phonemization, voicebank labeling, unit selection, timing solving, sample rendering, partial audio rendering, and the production native shell are Phase 2 and later work.

## Requirements

- CMake 3.25+
- A C++20 compiler
- Ninja for the supplied presets
- Python 3 for policy checks and evidence generation
- Optional: Inkscape for PNG preview generation

Tested in the provided Linux build environment with GCC. The CI definition also builds the portable Phase 1 libraries on Windows and macOS runners.

## Build

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Release and sanitizer builds:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

## Run the Phase 1 demonstration

```bash
./build/dev/seam_phase1_demo --output out/phase1
```

The demonstration:

1. creates a 10,000-note project;
2. performs a multi-note move, undo, and redo;
3. saves and reloads the canonical project;
4. verifies round-trip equality;
5. renders the visible piano-roll viewport;
6. simulates 256 audio callbacks.

Outputs:

```text
out/phase1/phase1-demo.seam.json
out/phase1/phase1-piano-roll.svg
out/phase1/phase1-summary.json
```

Generate all verification evidence, including a PNG preview when Inkscape is installed:

```bash
python3 scripts/generate_phase1_evidence.py --root .
```

## Benchmark

```bash
./build/dev/seam_phase1_benchmark
```

The benchmark measures index rebuild time and 1,000 viewport queries over a 10,000-note fixture. Results are environment-specific and are recorded as evidence rather than treated as a cross-machine guarantee.

## Repository policy

Only the `master` branch is allowed. The repository includes:

- `.githooks/pre-commit`
- `.githooks/pre-push`
- `scripts/verify_master_branch.py`
- CI branch-policy validation

The active repository is configured with:

```bash
git config core.hooksPath .githooks
```

## Architecture

```text
seam_core
    ↓
seam_domain
    ↓
seam_application
    ↓
seam_editor_ui       seam_formats       seam_platform
          \              |                 /
                  seam_phase1_demo
```

The domain has no dependency on iPlug2, Skia, JSON, SQLite, plugin SDKs, or operating-system APIs.

Detailed documents:

- [`PHASE1_IMPLEMENTATION_REPORT.md`](PHASE1_IMPLEMENTATION_REPORT.md)
- [`docs/architecture/OVERVIEW.md`](docs/architecture/OVERVIEW.md)
- [`docs/phase1/ACCEPTANCE.md`](docs/phase1/ACCEPTANCE.md)
- [`docs/brand/CHARACTER_DIRECTION_PHASE1.md`](docs/brand/CHARACTER_DIRECTION_PHASE1.md)
- [`docs/licensing/DEPENDENCY_POLICY.md`](docs/licensing/DEPENDENCY_POLICY.md)

## Ownership and licensing

Project source and first-party concept assets are currently proprietary and all rights are reserved. No production third-party source is vendored in Phase 1. Reference projects are documented separately and were used for behavioral study, not copied implementation.

See [`LICENSE`](LICENSE), [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and [`third_party/manifest.yml`](third_party/manifest.yml).
