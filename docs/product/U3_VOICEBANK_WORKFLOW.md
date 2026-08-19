# Usable Alpha U3 — Voicebank Workflow

## Purpose

U3 makes the secure voicebank infrastructure reachable from the standalone
application layer. It does not claim that the current technical fixture is a
musically complete demo bank. It establishes the product flow needed to
install, inspect, select, relink, replace, and diagnose a real bank later.

## User flow

```text
Install Voicebank…
→ verify Ed25519 signature and trusted signer
→ verify archive, entry, manifest, WAV and receipt hashes
→ transactional installation
→ catalog refresh
→ trust-aware browser card
→ exact track binding by ID/version/content hash
→ phoneme and pitch coverage diagnostics
→ production render or explicit phrase diagnostic
```

## Trust policy

- Trusted installed banks are selectable.
- Development fixtures are selectable only when the runtime explicitly allows
  development voicebanks.
- Untrusted installed banks remain visible but disabled.
- Character assets are informational and never affect selection or PCM.
- No missing, mismatched, or untrusted bank is silently replaced.

## Exact identity operations

### Select

The user chooses one exact catalog candidate. The track persists its ID,
version, and synthesis content hash through `SetTrackVoicebankCommand`.

### Relink

Relink adds an explicit search root and attempts to resolve the identity already
stored in the project. It never edits that identity.

### Replace

Replace intentionally changes the canonical track identity and is Undoable.
It is separate from Relink because it may change the resulting audio.

## Coverage diagnostics

Coverage inventory reports language, styles, root-pitch layers, unit kinds,
phone sequences, enabled/disabled counts, sustain, release, and breath support.
Region analysis distinguishes:

- missing unit;
- disabled matching unit;
- unsupported requested style;
- matching unit outside the configured pitch-distance limit.

Affected phrases return explicit diagnostics. Unaffected phrases and tracks may
continue rendering.

## Current UI boundary

The AppKit File menu exposes installation and a dynamic Voicebank menu. The
application controller also exposes browser cards, relink, replacement, and
coverage APIs for the upcoming full browser panel. Linux and Windows runtime
surfaces still require their target UI implementation and verification.
