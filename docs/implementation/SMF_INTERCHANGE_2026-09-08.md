# U31 bounded SMF interchange checkpoint

Status: codec foundation implemented and locally verified; native conversion
review, USTX and external-DAW interoperability are still open.

`libs/seam-interchange/src/smf_codec.cpp` now parses Type 0/1 PPQ Standard MIDI
Files with pre-allocation chunk checks, four-byte VLQs, running status, FIFO
overlap pairing, tempo/meter/lyric/text retention and explicit bounded loss
records for unsupported events. SMPTE timing, malformed data, hostile lengths,
zero tempo, invalid UTF-8 and trailing bytes fail without changing caller data.
The encoder produces deterministic Type-1 PPQ output with canonical equal-tick
ordering and validates every score field before allocation.

The focused `seam_smf_interchange_tests` target covers deterministic round-trip
notes/tempo/meter/text, equal-key overlaps, running status, dangling-note
diagnostics, truncation, SMPTE rejection, malformed VLQs, byte limits and
invalid-score immutability. Release focused execution passes; the same source is
also included in the core test target. This is a reusable bounded codec, not yet
an accepted import/export workflow: U29's held-file conversion boundary, U30
USTX, U32 native conversion review and real DAW/OpenUtau interoperability remain
required before Beta GO.
