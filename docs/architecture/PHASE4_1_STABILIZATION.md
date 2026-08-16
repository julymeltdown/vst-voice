# Phase 4.1 Architecture: stabilization before native platform integration

## 1. Context

Phase 4 connected concrete Unit renderers to a callback-shaped playback path. Phase 4.1 strengthens the invariants at five trust boundaries:

```text
Project/Voicebank files
Voicebank audio assets
Render cache identity and storage
Background worker publication
UI transport control → feeder → callback
```

The architecture remains a modular monolith. No network service or plugin process boundary is introduced.

## 2. Render identity pipeline

```text
Canonical Project
  └── extract phrase-scoped Project
       ├── Phonemizer revision + immutable phoneme result
       ├── Unit Selector revision + immutable Unit Plan
       ├── effective renderer/seam options
       └── selected Unit metadata
            └── resolve verified bank asset
                 ├── bounded byte read
                 ├── SHA-256 the byte buffer
                 └── decode and freeze audio from the same buffer

All fields + render ABI
  → canonical identity writer
  → SHA-256 contentHash
  → PCM cache key
  → same immutable plans consumed by RenderPipeline
```

The identity is content-addressed. `ProjectRevision` remains separate and determines whether a result is current for the UI.

## 3. Durable write state machine

```text
Create parent
  ↓
Optional durable backup of existing target
  ↓
Create unique temporary in target directory
  ↓
Write complete payload
  ↓
Durably flush temporary
  ↓
Atomic replace target
  ↓
Durably flush parent directory where supported
  ↓
Success
```

Temporary files are removed after pre-replacement failure. An error after atomic replacement cannot honestly claim that the old target remains; the old generation is retained at the configured backup path.

## 4. Untrusted-input budgets

```text
Project JSON        ≤ 64 MiB
Voicebank Manifest  ≤ 32 MiB
JSON                depth/node/string/entry budgets
PCM cache           fixed header + exact payload-size preflight
Voicebank asset     canonical containment + no symlinks
```

The parser uses exact signed integers and bounded recursion. These limits prevent memory amplification and stack exhaustion before semantic validation.

## 5. Playback control and reset ownership

```text
UI / native shell producer
  └── fixed-capacity SPSC ControlQueue
       └── dedicated feeder consumer
            ├── owns timeline / loop / playhead / playing
            ├── mixes into preallocated scratch
            └── writes audio ring

Audio callback consumer
  ├── owns ring read index
  ├── consumes reset request epoch
  ├── discards queued stale audio
  ├── acknowledges epoch
  └── reads or zero-fills output
```

No producer-side `clear()` mutates the consumer read index. When a reset is pending, the feeder waits for acknowledgment before publishing post-seek blocks.

## 6. Render completion gate

```text
submit revision N
→ render/cache work
→ optional cache publication
→ latestRevision check under scheduler mutex
→ completion queue check under scheduler mutex
→ only current result can carry PCM as Completed/CacheHit
```

An old content entry may remain in the disposable content cache; it cannot be announced as the current project output.

## 7. Command transaction boundary

```text
copy canonical Project
→ command apply/revert
→ validate Project
→ commit history mutation
```

Any failure restores the snapshot. History is mutated only after successful command and validation. This prevents partial state after rollback failure.

## 8. DSP boundary contract

All Unit renderers share the following structural contract:

```text
recorded onset/transition through stableStart
→ renderer-specific stable-vowel transform
→ recorded release
→ boundary SeamComposer
```

Renderer changes may alter stable-vowel character but must not silently replace the recorded consonant-to-vowel transition.

## 9. Phase 5 adapter implications

Phase 5 may attach:

- iPlug2 + Skia shell;
- Windows/macOS native text input;
- OS audio device;
- long-running feeder thread.

Those adapters must communicate through the stabilized ports. They must not bypass the playback control queue, directly clear ring indices, construct ad-hoc render cache keys, or implement independent file replacement logic.
