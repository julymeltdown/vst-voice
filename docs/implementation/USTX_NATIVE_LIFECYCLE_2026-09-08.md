# U30/U32 — Native USTX and conversion lifecycle (2026-09-08)

## Implemented

- `libs/seam-interchange/src/ustx_codec.cpp` provides a bounded YAML subset
  parser with line/flow support, duplicate-key rejection, depth/node/scalar/
  collection limits, UTF-8 validation and typed 0.9 records.
- `libs/seam-interchange/src/ustx_project_conversion.cpp` maps the typed record
  to the canonical 960-PPQ project and back. The mapping uses the authoritative
  tempo map for USTX millisecond pitch points, preserves note vibrato and emits
  explicit warnings/losses for unsupported controls.
- `libs/seam-authoring-runtime/src/interchange_service.cpp` adds the file
  boundary: extension selection, non-symlink path admission, bounded reads,
  source digest, side-effect-free import drafts, and create-new durable export.
- Standalone `AuthoringSession` exposes prepare/accept/export APIs. The
  application controller exposes `Open USTX or MIDI` and `Export Score`, with a
  required review callback before replacing the active document.

## Verification

`seam_ustx_interchange_tests` covers native parsing, conversion, deterministic
output, malformed/hostile input and explicit losses. The service target covers
source identity, import drafts, USTX create-new collision handling and SMF
round-trip. Release `seam_ustx_interchange_tests`,
`seam_interchange_service_tests`, `seam_smf_interchange_tests`, and the core
`seam_tests` suite pass after the implementation.

## Remaining acceptance

The supported subset still needs OpenUtau fixture breadth, a real native
conversion-review panel on both desktop platforms, handle-bound validation and
publication under the final file-IO threat model, and observed standalone/
embedded host workflows. A passing codec test is not a claim of complete USTX
field coverage or Beta GO.
