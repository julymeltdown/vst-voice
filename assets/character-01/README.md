# Character 01 runtime asset

This package is the Phase 5.1 canonical character integration asset.

- The character is a **voicebank / synthesis-product avatar**, not a singer or band frontwoman.
- Runtime use is restricted to welcome/voicebank identity, optional dock/status states, documentation, and packaging.
- The piano roll, Phoneme Lane, Unit Lane, Automation Lane, and Sample Microscope remain fully usable with character display set to `Off`.
- The source image was provided directly for this project and is converted to a low-cost PPM runtime portrait so the first-party software raster shell can load it without adding an image-codec dependency.

## Per-state art (0.2.0)

The six state portraits in `runtime/` are distinct development art, one per `states` entry in
`manifest.json`, derived by `scripts/build_character_state_art.py build` from the 640×960 masters
in `source/states/`. The same script cuts the six mouth sprites from the singing (`focused`) face and
writes `mouthPlacement`, so the overlay lines up with that portrait. `previews/state-contact-sheet.png`
shows the six states (top row, manifest order), the ring's circular crop of each (middle), and the
singing face with each mouth shape in `closed, narrow, nasal, open, wide, round` order (bottom).

| Package state | Shell state(s) | Pose |
|---|---|---|
| `neutral` | Idle, Listening | Calm, looking at the viewer, hands in hoodie pockets |
| `focused` | Singing | Eyes closed, singing, hand on chest |
| `rendering` | Rendering | Looking down in concentration, fingertips at chin |
| `complete` | Complete | Soft smile, small wave with the sleeve over the palm |
| `warning` | Warning | Uneasy side glance, arms crossed, lip bitten |
| `error` | Error | Head in hand, eyes shut |

## Eyes and per-mode outfits (0.3.0)

`eyes` maps each state to one or two boxes (normalized to the portrait, like `mouthPlacement`) around
that state's visible eyes. The idle blink closes a skin-toned lid from the top of each box, so it
lands on the eyes in every pose; a state without eyes gets no lid, and Reduce Motion keeps the blink
at zero. `previews/eye-boxes.png` draws the boxes over the shared (top) and SCENE (bottom) faces.

`outfits.<name>` lets a design mode supply its own `states` (all six), `mouths` and
`mouthPlacement`. The shell selects the outfit named after its design mode (`scene`), and uses the
shared set for any mode without one (EMO). An outfit's mouths are used only over its own states.
`outfits.scene` is the SCENE look: the same six poses with pink/cyan hair streaks and neon bead
bracelets, previewed in `previews/scene-state-contact-sheet.png`.
