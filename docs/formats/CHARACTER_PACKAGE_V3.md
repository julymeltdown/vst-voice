# Character package schema 3

Schema 3 keeps schema 2's six operating-state portraits, six performance mouth sprites,
`developmentOnly`, and optional normalized `mouthPlacement`. It replaces the sample-bank-only
association with one exact singer-resource identity:

```json
{
  "schemaVersion": 3,
  "characterId": "character.original.01",
  "displayName": "Original Singer",
  "version": "1.0.0",
  "voicebankId": "singer.original.01",
  "singerResource": {
    "kind": "procedural",
    "id": "singer.original.01",
    "version": "1.0.0",
    "contentHash": "<64 lowercase hexadecimal characters>"
  },
  "style": "Bright",
  "states": { "neutral": "...", "focused": "...", "rendering": "...", "complete": "...", "warning": "...", "error": "..." },
  "mouths": { "closed": "...", "narrow": "...", "nasal": "...", "open": "...", "wide": "...", "round": "..." }
}
```

`kind` is exactly `sample`, `procedural`, or `neural`. The `singerResource` identity must pass the
domain's normal resource-identity validation, including its exact content digest. `voicebankId` is
retained as an equal-to-`singerResource.id` compatibility alias for existing editor read models; it
does not weaken the exact match.

Render performance identity now carries the resource kind along with ID, version, and digest. A
schema-3 character follows and animates only when all four fields match the resource that produced
the published audio. Legacy schema-1/2 packages remain sample-bank-only and keep their existing
ID association. A character package's identity claim is not a signature, source-rights record, singer
qualification, or permission to ship; those remain separate trust and release gates.
