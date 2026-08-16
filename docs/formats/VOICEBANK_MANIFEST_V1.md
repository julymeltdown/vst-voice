# Voicebank Manifest Schema v1

## Root

```json
{
  "formatId": "com.project-seam.voicebank",
  "schemaVersion": 1,
  "id": "official.voice.01",
  "version": "1.0.0",
  "displayName": "Voicebank 01",
  "language": "japanese",
  "expectedSampleRate": 48000,
  "styles": ["original"],
  "units": []
}
```

## Unit

```json
{
  "id": "ja.original.e4.k-i.01",
  "alias": "k i",
  "phones": ["k", "i"],
  "kind": "cv",
  "audio": "audio/k-i-01.wav",
  "rootMidi": 64,
  "style": "original",
  "take": 1,
  "priority": 2,
  "gainDb": 0.0,
  "renderer": "raw",
  "enabled": true,
  "markers": {
    "audioOffset": 0,
    "consonantEnd": 2600,
    "vowelOnset": 3600,
    "stableStart": 5200,
    "loopStart": 7200,
    "loopEnd": 16800,
    "releaseStart": 19800,
    "audioEnd": 24000
  }
}
```

All source marker positions are integer frames in the unit WAV's native sample rate.

## Supported values

Unit kinds:

```text
cv, vcv, vc, vv, cc, sustain, release, breath, glottal, special
```

Renderer hints:

```text
raw, classic-psola, spectral-classic, stretch
```

Only `raw` is implemented in the Phase 2 render path. Other values are reserved schema vocabulary and cannot yet be dispatched by the production renderer.

## Security constraints

- audio paths must be relative;
- absolute paths and `..` components are rejected;
- the manifest may reference only data files;
- package extraction limits and signatures are future container work;
- a manifest is structurally validated before any unit audio is read.
