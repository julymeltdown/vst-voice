# Capability matrix: the refusal half, and why the positive half is refused

Date: 2026-10-03.

`docs/product/full-product-beta-contract.json` sets `scope.matrixStatus` to
`UNRESOLVED` and names U14, U19, U20, U35, U36 and U42 as the units that own it.
Its `scope.resourceCoverage` rule permits language subsets and backend
incompatibilities only inside a frozen matrix carrying "a supported tested
combination for every mandatory feature".

## Why this is not simply written

The rule turns on a **positive** claim: that some combination is supported and
tested. Nothing in this repository can support such a claim.

- There is no model or vocoder byte anywhere: `find . -name '*.onnx'` is empty.
- `scope.releasedResources` is `[]`.
- The only voicebank is an eight-unit public-domain fixture whose manifest declares
  `official: false` and whose README calls it unsuitable for release-quality singing.
- The one COMPLETE coverage report
  (`assets/pilots/seam-pilot-01/coverage-report.json`) carries a hardcoded
  `releaseEligible: false`.

Writing a positive cell would mean asserting acoustic qualification that no
artifact here supports. That is the misrepresentation this project has been
closing one unit at a time, so the positive half is stated as absent rather than
filled in.

## What is factual, and now captured

The negative half is not invented. It is read out of refusal paths that already
exist in the code:

| Capability | Carrier | Refused at |
| --- | --- | --- |
| formant | neural | `libs/seam-rendering/src/render_snapshot.cpp:810` |
| breathiness | neural | `libs/seam-rendering/src/render_snapshot.cpp:814` |
| tension | neural | `libs/seam-rendering/src/render_snapshot.cpp:819` |
| airiness | neural | `libs/seam-rendering/src/render_snapshot.cpp:822` |
| gender | neural | `libs/seam-rendering/src/render_snapshot.cpp:825` |
| growl | neural | `libs/seam-rendering/src/render_snapshot.cpp:828` |
| style-blend | neural | `libs/seam-rendering/src/render_snapshot.cpp:806` |
| style-blend | procedural | `libs/seam-rendering/src/render_snapshot.cpp:730` |
| style-blend | sample | `libs/seam-rendering/src/render_snapshot.cpp:1035` |
| backend | any | `libs/seam-rendering/src/render_scheduler.cpp:117` |
| formant | raw-renderer | `libs/seam-synthesis/src/raw_renderer.cpp:65` |

`tools/external_beta/capability_matrix.py` holds these eleven rows and states,
in its docstring and in its `why` field, that a combination absent from the
report is **not** claimed to work: absence means only that no refusal path was
found, which is never evidence of support.

## Why each row cites two places

A single citation could mean two different things. Each row carries the line
that raises an `ErrorCode::Unsupported` (proving the carrier refuses) and the
line where the message text is defined (proving the refusal says what the row
claims). The neural expression channels raise
`*UnsupportedMessage("neural model")` helpers whose text lives about 150 lines
away from the raise, which is why one citation was not enough. Where the code
raises a literal message the two citations are the same line.

`tests/external_beta/test_capability_matrix.py` opens each cited file and
asserts both. Removing or moving a cited refusal breaks the report rather than
leaving it quietly wrong.

## Limits

- `scope.matrixStatus` remains `UNRESOLVED`. This is a seed, not a matrix.
- Only refusals with a single unambiguous site are enumerated. The wider set in
  the synthesis and voice-design layers (phone-level articulation constraints,
  recipe schema bounds, raw-renderer onset rules) needs the same treatment
  before the refusal half could be called complete.
- The contract's `dictionary-original` and `character-original` have no C++
  representation at all. The module keeps them visible so the contract's six
  kinds are not quietly reduced to the three the code models.
- Which resource is authoritative for which requirement, and who owns that
  call, is an authority decision for the named owner units, not something this
  code can settle.
- Headless evidence on one machine. This says the report cannot drift from the
  code it describes. It claims nothing about quality.
