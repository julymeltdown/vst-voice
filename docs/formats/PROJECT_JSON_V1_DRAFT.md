# Project JSON Schema v1 Draft

## Purpose

The Phase 1 JSON format is a transparent development representation for round-trip and migration work. It is not yet the final `.seam` container.

## Root

```json
{
  "formatId": "com.project-seam.project",
  "schemaVersion": 1,
  "projectId": "hex-string",
  "name": "Project name",
  "ppq": 960,
  "tempoMap": [],
  "meterMap": [],
  "settings": {},
  "vocalTracks": [],
  "audioTracks": []
}
```

## IDs

IDs are encoded as hexadecimal strings to avoid loss of 64-bit integer precision in generic JSON tooling.

## Time

- note and region positions: integer ticks;
- tempo: finite BPM;
- project sample rate: floating-point Hz;
- future source audio markers: integer sample frames, not milliseconds.

## Canonical versus derived data

Stored:

- project input and user-authored state;
- voicebank and character references;
- notes, lyrics, tempo, meter, settings.

Not stored in Phase 1:

- spatial index;
- SVG geometry;
- command history;
- audio callback buffers;
- future generated phoneme/unit plan;
- render cache.

## Save protocol

The codec writes to a temporary sibling file, flushes it, removes the previous destination when required, and renames the temporary file into place. This Phase 1 mechanism is staged but is not claimed to be crash-atomic on every operating system. A later container implementation will add OS-specific atomic replacement, checksums, backup rotation, and schema migrations.
