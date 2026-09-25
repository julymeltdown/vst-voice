# English pronunciation resource

`cmudict.dict` is the CMU Pronouncing Dictionary from the official
`cmusphinx/cmudict` repository at immutable revision
`74790861f652b15e4ac49015a90074ad62a27690`.

- Upstream source: <https://github.com/cmusphinx/cmudict>
- Upstream archive SHA-256: `741c592660bbf10ab93fe3d5aa709b73a3b5ba91e3291a066715440f4137a58d`
- Upstream dictionary SHA-256: `81917843c7f44ce2b094ac63873c2c7a4cf802040792c455ba3ca406891c3d22`
- Vendored bytes are unmodified; the dictionary has 135,166 lines.
- License: BSD-2-Clause; see `licenses/third-party/CMUdict-2026-09-24-LICENSE.txt`.

The upstream resource describes North American English and disclaims
correctness guarantees. SEAM uses the first listed pronunciation variant,
preserves lexical stress, and retains the existing explicit-phone-hint override
path. Explicit hints take precedence over dictionary readings. The resource
improves lexical coverage but has not been reviewed for singing
pronunciation, every dialect, proper names, or every dictionary entry; those
remain explicit qualification work.
