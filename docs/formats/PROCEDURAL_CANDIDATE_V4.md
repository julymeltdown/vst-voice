# Procedural candidate v4 — initial unvoiced stops

Candidate v4 uses the v3 articulated metadata shape plus `plosiveRevision`
(a positive unsigned-32-bit algorithm revision), for 21 root fields. It adds
the marker kind `plosive`. A v4 candidate must contain both a plosive and an
oral vowel. Nasals and frications may coexist; legacy v1–v3 shapes remain intact.
Approval remains `unapproved`, with `planned-articulated-gestures` semantics.

A plosive marker binds an exact `p`, `t`, or `k` source in the frozen recipe's
selected style. Its start/end delimit the entire closure-plus-burst gesture.
At the candidate sample rate, burst frames are `llround(burstMilliseconds *
sampleRate / 1000)`. The planned release boundary is therefore end minus burst
frames; the closure must contain at least one frame and the complete gesture
must not exceed two seconds. Spectrum and source-clock bounds are checked at
the actual sample rate. No extra free-form release anchor is stored in v4.

These are generated timing anchors, not measurements of an actual speaker.
Later producer marker edits remain annotations of retained raw audio; they do
not regenerate or move the burst in that audio. The original immutable lineage
retains the recipe and initial timing.

## Render contract

The score path admits unvoiced onset tokens and explicitly released p/t/k coda
tokens with a resolved explicit or inferred start and a same-note vowel nucleus.
Codas require a post-nucleus start and a nonoverlapping preceding vowel.
It does not invent a missing nucleus,
borrow another style's source, truncate an oversized nominal burst, or treat a
stop as sustained frication. The burst occupies the tail of the onset; any
authored gap before the vowel is retained. The closure is exactly silent.

The aperiodic stream uses `PlosiveSource`'s own finite envelope. The voiced lane
is gated during the stop and fades back in at the vowel. Prefix replay, copied
checkpoints, resets and cancellation retain deterministic output. Cache identity
includes the plosive-source revision, articulation plan 7, articulated stream 7,
aperiodic stream 2, and procedural renderer 12. Configured unvoiced frication
bindings may also occupy a nonoverlapping post-nucleus coda span; their sustained
noise envelope remains distinct from the stop closure/burst model.

The source spectra are authored parameters, not qualified models of the three
places of articulation. Voiced stops, affricates, geminates and coarticulation
remain unfinished. Native plosive editing and source audition are wired but
still require live interaction qualification. A successful
render, bake, import or recovery does not establish intelligibility, musical
quality, source rights, redistribution permission or Beta GO.
