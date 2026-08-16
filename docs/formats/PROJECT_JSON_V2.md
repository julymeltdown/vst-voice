> **Superseded:** Schema 3 is current. See [`PROJECT_JSON_V3.md`](PROJECT_JSON_V3.md).

# Project JSON Schema v2

## Status

Schema 2 is the current transparent development representation. It is still not the final `.seam` container.

## Change from schema 1

Each vocal region gains `phonemeOverrides`:

```json
{
  "id": "3ec",
  "name": "Verse",
  "startTick": 0,
  "durationTick": 15360,
  "lyrics": [],
  "notes": [],
  "phonemeOverrides": [
    {
      "noteId": "3f0",
      "ordinal": 0,
      "symbol": "g",
      "startOffsetUs": -70000,
      "endOffsetUs": 0,
      "locked": true
    }
  ]
}
```

`symbol`, `startOffsetUs`, and `endOffsetUs` are optional. `locked` is required. `ordinal` is a zero-based phoneme ordinal within the note's generated sequence.

## Migration

The reader accepts schema 1 and schema 2.

- schema 1 region without `phonemeOverrides` becomes an empty collection;
- schema 2 requires the field to be an array;
- unsupported future schemas fail explicitly;
- generated phoneme sequences and unit plans are never migrated because they are not canonical.

## Timing units

- score position and duration: integer ticks;
- phoneme override timing: signed integer microseconds relative to note-on;
- voicebank source markers: integer sample frames;
- pixels and rendered rectangles: never serialized as canonical state.
