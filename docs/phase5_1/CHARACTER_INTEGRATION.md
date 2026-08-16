# Character 01 product integration

Character 01 is the product avatar for the first official voicebank and for SEAM's sample-splice synthesis identity. The character is **not a singer, idol, band vocalist, or render dependency**. Synthesis must remain bit-identical when character presentation is disabled or unavailable.

## Canonical asset

The canonical visual supplied for the project is stored at:

```text
assets/character-01/source/canonical-lowpoly.jpeg
```

Runtime portraits are pre-rendered PPM assets to keep the native shell independent from a 3D engine. They preserve the late-1990s/early-2000s low-poly visual direction and can later be regenerated from the canonical 3D source pipeline without changing synthesis code.

## Product surfaces

| Surface | Default | Purpose | Rule |
|---|---|---|---|
| First-run / Welcome | Full | establish official voicebank identity | may occupy dedicated non-editor space |
| Voicebank browser/card | Full or Minimal | identify the product attached to a voicebank | never substitutes technical metadata |
| Native editor | Minimal | persistent voicebank identity | user can cycle Full / Minimal / Off |
| Editor character dock | Full, opt-in | portrait + voicebank state | dedicated right dock; must not cover Piano/Phoneme/Unit/Automation lanes |
| Toolbar | Minimal | 48px identity portrait | shown only when Minimal is selected |
| Render status | state portrait | Rendering/Complete/Warning/Error feedback | presentation-only state |
| Voicebank Studio | textual binding now; portrait later | show which product IP belongs to the bank | microscope remains the dominant workspace |
| Installer / store / docs | Full | commercial product identity | may use higher-resolution promotional render |
| Exported audio | Never | no effect on user output | character state is not part of render identity |
| PCM cache key | Never | avoid presentation→audio coupling | character package/version is excluded |

## Display modes

```text
Full    dedicated editor dock with portrait and product metadata
Minimal compact toolbar portrait / voicebank identity
Off     no character presentation
```

`ProjectSettings.characterDisplay` stores the user's presentation choice. Character display does not affect Note, Phoneme, Unit, Seam, Renderer, Voicebank audio, Phrase snapshot, or PCM cache identity.

## Runtime states

```text
Neutral    idle project/voicebank identity
Focused    editing or playback focus
Rendering  a user-visible render is active
Complete   render/export completed successfully
Warning    recoverable voicebank/project issue or dirty state
Error      blocking voicebank/package/runtime issue
```

These are UI states, not personality or emotional-performance controls. They must not change the synthesized voice.

## Voicebank binding

Voicebank Manifest schema 3 adds an optional product binding:

```json
{
  "characterId": "official.character.01",
  "characterVersion": "0.1.0"
}
```

Both values must be present together or both empty. The binding identifies the intended product avatar; it does not force the package to be installed. A third-party voicebank may ship with no character binding.

## Architecture boundary

```text
Voicebank data -----> synthesis / render identity
      |
      +---- characterId/version ----> Character registry/presentation

Character package -----------------> native UI only
Character package --------X--------> synthesis
Character package --------X--------> audio callback
Character package --------X--------> render cache identity
```

The editor must still open and synthesize when a bound character package is missing. The UI should report the missing presentation asset and fall back to text-only voicebank identity.

## Phase 8 platform-shell parity

Windows, macOS, and Linux native shells consume the same `CharacterPresentation` model and the same packaged runtime portraits. Operating-system adapters may present pixels and input differently, but they may not redefine character states, placement rules, or audio dependencies.

```text
Shared CharacterPresentation
        ├── X11 shell
        ├── Win32 shell
        └── AppKit shell
```

Platform-specific code therefore does not:

- choose a different Character 01 personality or costume;
- place the character over technical lanes;
- add the character to routing, recording, cache, package trust, or export state;
- force Full mode when the user selected Minimal or Off.

Physical platform acceptance must check that the dedicated dock and compact identity surface scale correctly at each operating system's DPI/backing scale, but a rendering difference must not alter product policy.
