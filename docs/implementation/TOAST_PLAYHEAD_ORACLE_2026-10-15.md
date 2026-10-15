# Toast/playhead oracle correction — 2026-10-15

Focused engineering PASS; independent review pending; full suite after repairs
NOT_RUN. This changes one native test, not production rendering.

The [current full baseline](DIAGNOSTICS_MINIMUM_LAYOUT_2026-10-15.md) failed the
case in `tests/test_character_state_art.cpp` that asserts the lane playhead stays
behind both error and diagnostic toasts. Its first assertion demanded a visible
lane segment above the error card. That assumption conflicts with the actual
wrapped, stacked card geometry at the test's 1600x900 viewport.

From source base `0d91f7490e663720a79ad953ee4cf1567ae36343`, an observational
test edition added only raster capture and rectangle/pixel logging before the
original assertion. A full dependent Release build passed in 25.655934 seconds.
The filtered native case then reproduced that original failure (exit 1) in
0.617633 seconds while recording:

| Rectangle / observation | Actual value |
| --- | --- |
| Lane plot vertical interval | 740–836 |
| Error card vertical interval | 738–794 |
| Diagnostic card vertical interval | 802–850 |
| Visible plot gap sampled | 796–800 |
| Maximum channel change in the gap | 139 |
| Maximum channel change in error / diagnostic cards | 4 / 4 |

The card starts above the lane, so the former `plot.y < error.y - 4` assertion
cannot hold. Production layering already keeps the playhead under the cards.
The final test requires that both cards actually overlap the line's plot,
samples a nonempty visible gap between them, and retains the original thresholds:
visible line change **>40**, each card change **<=8**. It still compares a cached
shell without and with a playhead at the same settled UI clock. Neither a missing
line nor a line painted over a toast can pass these assertions.

The optional existing `SEAM_CHARACTER_PREVIEW_DIR` hook now retains this case's
two raster frames and emits its measured geometry. The final full dependent
build passed in **4.254323 seconds**; the complete character-surface CTest lane
passed all **46 native cases** in **5.474689 seconds**. Its without/with PPM pair
is byte-for-byte identical to the observed failing edition's pair. Geometry and
139/4/4 measurements are also identical. The old assertion failed and the correct
occlusion assertions passed against the same rendered output.

The evidence root is `out/evidence/toast-playhead-oracle-2026-10-15/`: **37
artifacts, nine preserved pins**, manifest 8,633 bytes, SHA-256
`9ec6bb9261da56cd06aed0fef3f20efa294e7e055b2dfdb967610d06d32d6739`.
It retains original/observational/final test sources, three selected unchanged
production sources, both executing binaries, failed/passing receipts and output,
13 PPM frames and one PNG. The observed PNG was inspected. The read-only verifier
passed identities, Git source binding, failure site, pass count and raster
equality checks. These selected local inputs are not full build provenance.

The unchanged sources are `sing_shell.cpp`, `character_surface.cpp` and
`sing_layout.cpp`. No production source changes are part of this commit. This
is a native raster test, not human/VoiceOver/DAW acceptance. Capability citations
remain the next baseline failure to fix, followed by full Release verification.
Audio admission and canonical gates stay unchanged: Alpha 0/20, U45 HELD, Beta
BLOCKED, release eligibility false; .github and the Windows README TODO untouched.
