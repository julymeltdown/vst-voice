# D1 listening packet: rendered, listening pending

Artifact root: `/Users/lhs/Downloads/seam-listening-artifacts/2026-09-15-d1-02`.
Source commit: `b6e5158` (full identity and all file hashes in `manifest.json`).
The artifact manifest SHA-256 is
`1967059afb58d6e3b8d9da03ca79cca72540312912b767b8b020b6903022d700`.

The existing production pilot rendered 11 cases, each with baseline, higher-formant
and breathier recipe variants. Each variant retains a stereo master and dry mono
candidate, for 66 WAVs total. The 30-note unfamiliar melody is eight bars/16 seconds,
with unequal durations and a held final vowel. All 335 artifact hashes were checked;
none differed. All samples were finite and none clipped. These are technical results.
The 10.53-second total includes export, multiple outputs and pitch analysis; it is not
an isolated synthesis realtime benchmark or proof of live DAW latency.

The melody was rerendered with the retained executable into the separate directory
`/Users/lhs/Downloads/seam-listening-artifacts/2026-09-15-d1-repeat`; all six output
WAV hashes match. The retained macOS executable still names absolute Homebrew/system
dependencies. A copied libcrypto and dependency list are retained, but no portable
application or long-term OS compatibility is claimed.

## Listening observation

Status: **NOT_REVIEWED**. No listener transcription, musician judgment or unaided
creator observation has been supplied. The kana phrases are development screening
material, not a language-reviewed corpus. Neither A, B, C nor D is assigned yet.
Pitch diagnostics can identify cases for inspection but cannot supply those judgments.

Start audition with `unfamiliar-song/baseline/master.wav`, then compare its two recipe
variants. For phonetic diagnosis, use the mono candidate WAVs under each case's
`candidates/` directory. These are direct generated audio; no installed-bank comparison
was performed. The earlier articulation-only smoke output remains in sibling directory
`2026-09-15-d1-01` and is not part of this frozen packet.

## Next decision

Proceed to D2 using the retained melody and recipe. Do not expand inventory or choose
an acoustic repair merely from finite samples or the preparable inventory count.
Collect actual listening/creator observations, then apply the revised plan's outcome
matrix and two-cycle repair budget. Preserve this unreviewed technical reference;
generate future output alongside it rather than overwriting it.

Preparation discovered and repaired a diagnostic defect: custom rhythmic phrases
were scored with fixed 480-tick pitch windows. The pilot now derives the central-half
window from each note's actual cumulative start/duration and reports frame/tick bounds.
The full pilot CLI regression passes, including the unequal-duration window assertions.
This changes the reliability of measurements, not the synthesis or its musical quality.
