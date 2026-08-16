# Voicebank Manifest v3

Schema 3 extends v2 with an **optional Character product binding**. Audio synthesis data is otherwise unchanged.

```json
{
  "formatId": "com.project-seam.voicebank",
  "schemaVersion": 3,
  "id": "official.voice.01",
  "version": "1.0.0",
  "displayName": "Official Voicebank 01",
  "characterId": "official.character.01",
  "characterVersion": "0.1.0",
  "language": "japanese",
  "expectedSampleRate": 48000,
  "styles": ["original"],
  "units": []
}
```

## Character binding rules

- `characterId` and `characterVersion` are both non-empty, or both empty.
- Empty values mean the voicebank has no official character product binding.
- The binding is presentation metadata only.
- Missing character packages do not make a voicebank invalid or prevent synthesis.
- Character package bytes are excluded from render/cache identity.
- Third-party voicebanks may remain characterless.

## Migration

- v1 loads with no Pitch Marks and no character binding.
- v2 loads with Pitch Marks and no character binding.
- v3 writes both character fields, using empty strings when no binding exists.
