# U41 — Character performance-state binding (2026-09-08)

The native editor now derives the Character 01 state asset from the same
verified `VoiceIdentityView` and render lifecycle used by the editor status
surface. Missing voice identity maps to warning, active rendering maps to the
rendering asset, a completed render uses complete, actionable failures use the
error asset, and a verified ready singer uses focused while playing and
neutral while stopped. The host-owned `CharacterPresentation` then selects
the actual loaded asset, keeping texture decoding outside audio processing.

The change removes the previous state mismatch in which the character remained
neutral/focused while the singer was rendering or failed. A native regression
checks rendering and ready transitions on a verified character binding.

This is a semantic/runtime-state repair only. It does not claim mouth-shape
animation, phoneme-synchronized performance poses, production character
rights, or clean installed-platform evidence; those remain required before
U41/U42/U47 can qualify Character 01 for Beta GO.
