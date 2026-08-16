# Voicebank Manifest Schema v2

## Status

Schema 2 is the current data-only voicebank manifest used by the Phase 3 synthesis path.

## Root

```json
{
  "formatId": "com.project-seam.voicebank",
  "schemaVersion": 2,
  "id": "official.voice.01",
  "version": "1.0.0",
  "displayName": "Voicebank 01",
  "language": "japanese",
  "expectedSampleRate": 48000,
  "styles": ["original"],
  "units": []
}
```

## Unit with pitch marks

```json
{
  "id": "ja.original.e4.k-i.02",
  "alias": "k i",
  "phones": ["k", "i"],
  "kind": "cv",
  "audio": "audio/k-i-02.wav",
  "rootMidi": 64,
  "style": "original",
  "take": 2,
  "priority": 0,
  "gainDb": -1.5,
  "renderer": "classic-psola",
  "enabled": true,
  "markers": {
    "audioOffset": 0,
    "consonantEnd": 2800,
    "vowelOnset": 3800,
    "stableStart": 5600,
    "loopStart": 7600,
    "loopEnd": 21800,
    "releaseStart": 24500,
    "audioEnd": 30000
  },
  "pitchMarks": [
    {"frame": 5600, "confidence": 0.98, "locked": false},
    {"frame": 5709, "confidence": 0.98, "locked": false}
  ]
}
```

Pitch marks are integer frames in the unit WAV's native sample rate. Confidence is finite and normalized. Marks must be strictly ordered, unique, and inside the unit's editable audio range. `locked` records deliberate manual placement.

## Migration

The reader accepts schema 1 and schema 2.

- schema 1 units receive an empty pitch-mark collection;
- schema 2 requires `pitchMarks` to be present as an array;
- a unit may remain valid without marks, but Classic PSOLA will reject or explicitly fall back for that unit;
- unsupported future schemas fail explicitly.

## Renderer behavior in Phase 3

```text
raw               implemented
classic-psola     implemented
spectral-classic  schema vocabulary; explicit Raw fallback only
stretch           schema vocabulary; explicit Raw fallback only
```

Fallback is represented in placement diagnostics and is never silently presented as the requested renderer.

## Security constraints

- audio paths must be relative;
- absolute paths and `..` components are rejected;
- manifests and future packages are data-only;
- executable, script, shader, and arbitrary web content are not valid voicebank assets;
- structural validation runs before unit audio is read;
- package extraction limits and signatures remain future container work.
