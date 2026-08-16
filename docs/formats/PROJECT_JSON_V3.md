# Project JSON Schema v3

## Status

Schema 3 is the current transparent development representation. It is not yet the final zipped `.seam` container.

## Changes from schema 2

Each vocal region now carries three additional canonical user-intent collections:

```json
{
  "unitSelectionOverrides": [],
  "seamOverrides": [],
  "pitchAutomation": []
}
```

Generated phonemes, automatic unit plans, timing plans, phrase segments, and rendered PCM remain derived state and are not serialized.

## Unit selection override

```json
{
  "noteId": "3f0",
  "ordinal": 0,
  "tokenCount": 2,
  "unitId": "ja.original.e4.k-i.02",
  "renderer": "classic-psola",
  "locked": true
}
```

- `noteId` and `ordinal` identify the first generated phoneme covered by the unit.
- `tokenCount` is the exact number of consecutive phoneme tokens covered.
- `unitId` must resolve in the selected voicebank at render time.
- `renderer` is one of `inherit`, `raw`, `classic-psola`, `spectral-classic`, or `stretch`.
- Unsupported renderers may fall back only through an explicit dispatcher policy and must report that fallback.

## Seam override

```json
{
  "noteId": "3f1",
  "ordinal": 0,
  "seamAmount": 0.91,
  "overlapUs": 12000,
  "phaseReset": 0.75,
  "envelopeBlend": 0.15,
  "curve": "hard-character",
  "locked": true
}
```

The key identifies the first phoneme of the incoming unit. Optional numeric values preserve the track/region default when omitted.

Supported curves:

```text
smooth
linear
equal-power
hard-character
```

## Pitch automation

```json
{
  "tick": 960,
  "cents": -18.0,
  "interpolation": "smooth"
}
```

Pitch points are region-relative integer ticks and signed cent offsets. Points are unique by tick and sorted. Supported interpolation values are `step`, `linear`, and `smooth`.

## Migration

The reader accepts schemas 1, 2, and 3.

- schema 1 gains an empty `phonemeOverrides` collection;
- schema 2 gains empty unit-selection, seam, and pitch-automation collections;
- schema 3 requires all four region override collections to be present and correctly typed;
- unsupported future schemas fail explicitly.

## Timing units

- score position and duration: integer ticks;
- phoneme and seam time overrides: signed integer microseconds;
- voicebank source markers and pitch marks: integer source sample frames;
- rendered PCM: integer destination sample frames;
- pixels and render cache entries: never serialized as canonical project state.
