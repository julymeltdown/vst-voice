# `.seambank` Binary Package Format v1

`.seambank` is a deterministic, data-only voicebank container. It does not contain executable code, native plug-ins, scripts, shaders, or remote web content.

## Layout

All integer values are little-endian.

```text
Header (40 bytes)
Entry table (canonical path order)
Payload bytes (contiguous entry order)
Embedded Ed25519 public key (32 bytes)
Ed25519 signature (64 bytes)
```

The signature is produced over the 32-byte SHA-256 digest of every byte before the signature, including the embedded public key. Each entry also has an independent SHA-256 checksum. The package verifier validates the container signature, table ordering, section extents, every entry checksum, `manifest.json`, and all manifest-referenced audio assets.

## Entry table

Each entry stores:

```text
uint16 pathLength
pathLength UTF-8 bytes
uint64 absolutePayloadOffset
uint64 payloadSize
byte[32] SHA-256
```

Paths use `/`, are relative, have no empty, dot, dot-dot, hidden, drive, or backslash segments, and are sorted lexicographically. Duplicate and overlapping entries are rejected.

## Allowed data

The v1 allowlist covers voicebank metadata, PCM WAV, analysis data, dictionaries, presets, text licenses, and pre-rendered character images. Executable and script extensions are rejected.

## Trust

A valid embedded signature proves package integrity and identifies the signing key. Installation additionally requires the signer public key to be present in an explicit trust set. Merely embedding a self-selected key does not make a package trusted.
## Character binding

When the signed voicebank manifest declares both `characterId` and `characterVersion`, the package must contain `character/manifest.json`. Its character ID, version, and `voicebankId` must match the signed voicebank manifest. Every runtime state path declared by the character manifest must resolve to a safe data entry under `character/`. Character assets remain presentation data and do not participate in synthesis or PCM cache identity.

Durable editor backup generations such as `manifest.json.bak` are working-directory artifacts and are excluded from package collection rather than signed.
