# Character 01 Bible — Phase 1 Draft

## Product role

Character 01 is the visual avatar of Official Voicebank 01 and Project SEAM's concatenative model. She represents cut samples, repeated vowels, explicit seams, and controlled discontinuity. She is not assigned a fictional band career or singer biography.

## Personality range

```text
reserved ↔ sudden overreaction
careful organization ↔ visible imperfect repair
emotional distance ↔ intense focus
quiet exterior ↔ loss of composure
```

She is not permanently sad, hostile, fragile, or elitist. She does not police other people's musical knowledge.

## Canonical silhouette constraints

- dark asymmetric layered hair;
- natural human proportions adapted to an early-3D game model;
- hoodie/cardigan and slim trousers as the base;
- practical low-profile footwear;
- no microphone, instrument, or stage pose;
- strong accessories limited to three or fewer;
- one accent color family per canonical outfit.

## Product-system motifs

- seams correspond to actual garment construction;
- splice marks appear as restrained stitch breaks or fabric boundaries;
- small unit-index graphics may refer to `CV`, `VC`, and `VV` without becoming slogans;
- asymmetry represents sample branching;
- visibly different dark fabric values represent different source units.

## Current status after Phase 11

The violet low-poly direction under `assets/character-01/source/canonical-lowpoly.jpeg` is the current canonical runtime direction. The product binding and Full/Minimal/Off policy are implemented. The following are still not locked for commercial release:

- final public character name;
- production-quality front/side/back turnaround and topology;
- final facial model, expressions, LODs and animation;
- complete IP ownership/assignment review;
- trademark, domain and social-account clearance;
- commercial key art and merchandise rules;
- final relationship statement between Character 01 and the eventual voice provider.


## Product integration rule (Phase 5.1)

Character 01 is the first official voicebank's product avatar, not a singer persona. The product must use the character consistently but selectively:

- full art on Welcome, voicebank product pages, installer/store/docs;
- optional full dock in the editor;
- minimal portrait in the toolbar/voicebank identity surface;
- state portrait for render complete/warning/error feedback;
- textual product binding in technical Voicebank Studio surfaces;
- never cover Piano Roll, Phoneme, Unit, Automation, Waveform, or Spectrogram editing areas;
- never participate in DSP, rendering, PCM cache keys, or exported audio;
- Full/Minimal/Off is always a user choice.

The canonical low-poly visual source for Character 01 is maintained under `assets/character-01/source/` and runtime assets are derived from that source.

## Native platform consistency (Phase 8)

Character 01 is not re-authored per operating system. X11, Win32, and AppKit shells all render the shared pre-rendered Character Package and respect the same Full/Minimal/Off state. OS-specific code owns only the window, display, input, and device boundary.

Windows/macOS platform work must not turn Character 01 into a desktop assistant, talking mascot, singer overlay, or mandatory always-visible panel. The main editor remains a professional technical workspace; the optional dedicated dock is the maximum persistent presentation surface.
