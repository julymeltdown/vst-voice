# Source creation selects its editing context

After successfully adding a frication or stop source, Studio now selects its first
parameter row. Existing scrolling and semantic-tree construction then expose the
source controls and preview actions without manual paging. Selection occurs only
after successful creation; rejected or cancelled creation does not run the new
selection path.

The shared `designerSourceSelection` helper resolves recipe-global source indices
into the style-filtered parameter list. It accounts for oral formants, preceding
same-style frication sources and preceding same-style stops. Missing source/style
targets return no selection instead of an out-of-range row.

Session source creation also preserves the selected vowel when its style already
matches the new source. Previously it always selected the first matching pose,
changing a chosen vowel unnecessarily. A different source style still selects
the first matching pose, and audition pitch remains unchanged.

Regression tests cover mixed styles, preceding source offsets, missing indices,
empty pose lists, recipe non-mutation, compatible vowel retention, pitch
retention, style switching and duplicate-source rejection. No recipe schema,
source DSP, approval or producer state changes are introduced.

Full Release build and Designer/core CTest entries passed 2/2 in 22.29 seconds;
`git diff --check` passed. This is focused regression evidence, not a full-suite
or release-platform qualification.

Follow-up live verification confirmed automatic scrolling and highlighted source
selection for both frication and stop creation in the rebuilt 720x520 app. Native
keyboard focus remains unqualified because the UI tool rejected the key command.
See `DESIGNER_POST_CONTEXT_REGRESSION_2026-09-09.md` for the exact boundary.
This is a partial U22 workflow improvement, not full Beta GO acceptance. Existing
dirty work is preserved, with no staging, commit or push.
